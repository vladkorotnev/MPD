/*
 * Copyright (C) 2003-2011 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Maps directory and song objects to file system paths.
 */
#include "config.h"
#include "mapper.h"
#include "directory.h"
#include "song.h"
#include "path.h"
#include "audio_format.h"
#include <glib.h>

#include <assert.h>
#include <string.h>
#include <sqlite3.h>

#ifdef SSD_CACHE
#include "database.h"
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include "conf.h"
#include "wimpmain.h"
#endif
#include "au_pl_task.h"

extern int dsd_pcm_mode;

static char *music_dir;
static size_t music_dir_length;

static char *playlist_dir;
int debugFlag= 0;
#ifdef SSD_CACHE
static char *cache_dir;
int g_sample_rate = 0;
int g_changing_rate = 0;
static char g_audio_mode = 0;
static char *g_fifo_mmap;
static bool g_is_w20 = false;

bool g_cache_disabled = false;
#endif

/**
 * Duplicate a string, chop all trailing slashes.
 */
static char *
strdup_chop_slash(const char *path_fs)
{
	size_t length = strlen(path_fs);

	while (length > 0 && path_fs[length - 1] == G_DIR_SEPARATOR)
		--length;

	return g_strndup(path_fs, length);
}

static void
mapper_set_music_dir(const char *path)
{
	music_dir = strdup_chop_slash(path);
	music_dir_length = strlen(music_dir);

	if (!g_file_test(music_dir, G_FILE_TEST_IS_DIR))
		g_warning("music directory is not a directory: \"%s\"",
			  music_dir);
}


static void
mapper_set_playlist_dir(const char *path)
{
	playlist_dir = g_strdup(path);

	if (!g_file_test(playlist_dir, G_FILE_TEST_IS_DIR))
		g_warning("playlist directory is not a directory: \"%s\"",
			  playlist_dir);
}
#ifdef SSD_CACHE
#define MMAP_FILE "/tmp/.mpd_fifo_mmap"
#define WRITING_SSD '3'
#define WRITE_SSD_PAUSED '2'
#define READING_SSD '1'
#define DONE_READING_SSD '0'
#define WL_BUFF_SIZE (1024*1024*6)
struct {
    char *buffer;
    size_t start_offset;
    int ncached;
    int fd;
    bool is_eof;
} g_wl_buffer;

bool
is_nas_lnk(const char *uri)
{
    if (strncmp(uri, "lnks/", 5) == 0 
        ||strncmp(uri, "/hdds/lnks/", 11) == 0 ) {
        //return true;
        return false;
    }
#ifdef WIMP_SUPPORT
    else if (strncmp(uri, "/srv/cache/tidal://", 19) == 0 || strncmp(uri, "/srv/cache/qobuz://", 19) == 0 || strncmp(uri, "/srv/cache/bugs://", 18) == 0 || strncmp(uri, "/srv/cache/shout://", 19) == 0) {
        return true;
    }
#endif
    else
        return false;
}

/* 
   how about for NAS play and HDD play
   here's how it should work
   cache at the first read, then everytime it reads, we feed from buffer.
   only cache for: OR just cache every file from SSD
   96khz for higher files from SSD (not from HDD/NAS)
   96/192k WAV read size: 12288
   192k aiff : 12288
   96/192k flac : 8188
   24bit 44.1 m4a : 512
   192k m4a : 8188
*/

void wl_buffer_init()
{
    g_wl_buffer.buffer = (void*)malloc(WL_BUFF_SIZE);
    if(g_wl_buffer.buffer == NULL){
        g_warning("fail to init wl buffer");
    }
}

void wl_buffer_finish()
{
    unmark_ssd_reading();
    if(g_wl_buffer.buffer){
        free(g_wl_buffer.buffer);
        g_wl_buffer.buffer = NULL;
    }
}

void wl_buffer_clear()
{
    //memset(g_wl_buffer.buffer,0,sizeof(g_wl_buffer));
    if(g_wl_buffer.buffer)
        memset(g_wl_buffer.buffer,0,WL_BUFF_SIZE);
    g_wl_buffer.fd = 0;
    g_wl_buffer.start_offset = 0;
    g_wl_buffer.ncached = 0;
    g_wl_buffer.is_eof = 0;
}

/*
  we will need a refill when:
  fd is different
  offset+size > buffer cached.
 */
bool wl_buffer_need_refill(int fd,int offset,int size)
{
    //NOTE: when fifomanager is not caching we will fall back to normal read and it will always return true
    if(g_wl_buffer.fd != fd)
    {
        //g_debug("FD changed, need refill:%d vs %d",g_wl_buffer.fd,fd);
        return true;
    }
    if(g_wl_buffer.start_offset+g_wl_buffer.ncached < offset+size)
    {
        //g_debug("offset out of range : start+cached = %d < request :%d, need refill",
        //          g_wl_buffer.start_offset+g_wl_buffer.ncached,offset+size);
        return true;
    }
    //happens when decoder finish tag parsing and start to fetch music data
    if(offset < g_wl_buffer.start_offset){
        //g_debug("offset out of range : start: %d > reqeust: %d, need refill",
        //          g_wl_buffer.start_offset,offset);
        return true;
    }

    return false;
}


/*
  clear and fill the buffer from new offset. we will hold, until the buffer is filled,
  before that, the player can use the buffer from music pipe.
  we will fill the times of size
  no need to move the fd offset back,we will continue read from here.
*/
bool wl_buffer_fill(int fd,size_t offset,size_t size)
{
    // wl_buffer_clear();
    //int nchunk = 1;
    int nchunk = WL_BUFF_SIZE/size;
    size_t ncached = 0;
    size_t need;
    struct timeval tv1,tv2;
    gettimeofday(&tv1,NULL);
    need = nchunk*size;
    //    g_debug(">>will hold %d chunk for %d bytes = %d bytes total, ioffset:%d , foffset:%d",nchunk,size,need,offset,lseek(fd,0,SEEK_CUR));
    if(!is_write_paused()&&!is_marked_reading()) //it coulde be ready when headsup
        mark_ssd_reading();
    //g_debug("WAIT SSD READY");
    int i = 0;
    const int MAX_WAIT_US = 1;
    while(!is_write_paused()&&i<MAX_WAIT_US){
        i++;
        usleep(1);
    }
    if(i >= MAX_WAIT_US){
        //g_debug("xxxxxxxxxxxxxxxxToo long to wait , fall back to normal read to avoid underrun %c",g_fifo_mmap[0]);
        return false;    //read normally
    }
    //g_debug(">>>write paused after %d count",i);  //if we wait too long we'd better drop this cache and do normal read
    //g_debug("{{{{{{{{{{{{caching}}}}}}}}}}}}");

    ncached  = read(fd,g_wl_buffer.buffer,need);
    //    g_debug(">>foffset after read:%d",lseek(fd,0,SEEK_CUR));
    
    /*
      sometime we need another quick refill after this one because decoder asked something elsewhere out of range.
      and it is cause int, to avoid this ,we only unmark the reading after we succed played one chunk from cached.
      which means it has good chance to keep read and use the rest of the cached buffer
    */
    unmark_ssd_reading();
    if(ncached < 0){
        g_debug("Fail to fill buffer");
        return false;
    }
    else{
        if(ncached == 0){// && lseek(fd,0,SEEK_CUR)<total){
            g_debug("NOT EOF but get 0 byte, retry now,fd:%d",fd);
        }
        g_wl_buffer.fd = fd;
        g_wl_buffer.ncached = ncached;
        g_wl_buffer.start_offset = offset;
        gettimeofday(&tv2,NULL);
        g_debug("Cached %d bytes => %d * %d in %ds:%dus",ncached,size,nchunk,tv2.tv_sec-tv1.tv_sec,
                  tv2.tv_sec>tv1.tv_sec?(tv2.tv_usec>tv1.tv_usec?tv2.tv_usec:tv1.tv_usec):tv2.tv_usec-tv1.tv_usec);//approximate usec
        if(size > ncached){
            g_debug("EOF:%d > %d",size,ncached);
            g_wl_buffer.is_eof = true;
        }
        else{
            g_wl_buffer.is_eof = false;
        }
        //if succed fill buffer, restore offset
        //lseek(fd,-ncached,SEEK_CUR);  //read return 0 after restore this way
        lseek(fd,lseek(fd,0,SEEK_CUR)-ncached,SEEK_SET);  //read return 0 after restore this way
        //        g_debug(">>foffset after restore:%d",lseek(fd,0,SEEK_CUR));
        return true;
    }
}

/*
  when read, first check whether we cached the offset
  parameters:
  fd: fd from input stream (the file we want to read)
  ptr: the buffer we will fill
  size: the size of buffer decoder wants

  can we get our own copy of fd/offset(or restore it once we done?), so we can always compare with mpd's read and check

  we need to restore fd (lseek) and increase it once a time when we successfully feed buffer to decoder.
  so when failed to do that, we can always refill the buffer. (chunk size asked by decoder can change, for tag/music, etc
 */
size_t wl_buffer_read(int fd, int offset, void *ptr,size_t size)
{
    size_t ret = 0;
    if(wl_buffer_need_refill(fd,offset,size)){
        if(wl_buffer_fill(fd,offset,size)==false){
            ret = read(fd,ptr,size);
            return ret;
        }
    }
    /*
      g_debug(">>>>foffset:%d",lseek(fd,0,SEEK_CUR));
      ret = read(fd,ptr,size);
        g_debug(">>foffset:%d",lseek(fd,0,SEEK_CUR));
        g_debug("FIRST READ:%d:%d:%d",ret,lseek(fd,0,SEEK_CUR),ret);
    //    lseek(fd,-ret,SEEK_CUR);
        lseek(fd,lseek(fd,0,SEEK_CUR)-ret,SEEK_SET);
        g_debug(">>foffset:%d",lseek(fd,0,SEEK_CUR));
        ret = read(fd,ptr,size);
        g_debug("<<<<foffset:%d",lseek(fd,0,SEEK_CUR));
    return ret;
    */
    
   	//assert(offset>g_wl_buffer.start_offset);
   	
    int copy_from = offset-g_wl_buffer.start_offset;

    // before we actually read at next time , let fifomanager get ready 
    int nread = 40;
    if(copy_from >= g_wl_buffer.ncached - size*nread){
        if(!is_marked_reading()&&!is_write_paused()){
            //g_debug("!!!!!!!!!!!!!!!!hEADsUP!!!!!!!!");
            mark_ssd_reading();
        }
        else if(is_write_paused()){
            //g_debug("!!!!!!!!!!!!!!!!Ready>>>>>>>>>>>");
        }
        else if(is_marked_reading()){
            //g_debug("!!!!!!!!!!!!!!!!NOT Ready<<<<<<<<<<<<<<<");
        }
    }
    /*    if(copy_from > size*3 && copy_from < size*5){
        //means we're reading from buffer. release the reading mark
        if(is_write_paused()){
            g_debug("Relase reading =========");
            unmark_ssd_reading();
        }
        }*/
    g_debug(">>Feeding %d bytes:%ld:%ld:%ld",size,offset,g_wl_buffer.start_offset,copy_from);
    if(g_wl_buffer.is_eof){
        size_t nleft = g_wl_buffer.start_offset+g_wl_buffer.ncached-offset;
        g_debug(">>Feeding from 1 %d",copy_from);
        g_debug(">>Feeding %d bytes:%ld:%ld:%ld",size,offset,g_wl_buffer.start_offset,g_wl_buffer.ncached);
        memcpy(ptr,g_wl_buffer.buffer+copy_from,nleft);
        g_debug("%d bytes to read before  EOF",nleft);
        ret = nleft;
    }
    else{
        //        g_debug(">>Feeding from %d",copy_from);
        assert(g_wl_buffer.ncached >= (offset-g_wl_buffer.start_offset+size));
        //        g_debug(">>Feeding from %d to 0x%0x",copy_from,ptr);
        memcpy(ptr,g_wl_buffer.buffer+copy_from,size);
        //        g_debug(">>Feeded",size);
        ret = size;
    }
    //if succeed feeding, increase file offset
    //    g_debug(">>foffset before INC:%d",lseek(fd,0,SEEK_CUR));
    lseek(fd,ret,SEEK_CUR);
    //    g_debug(">>foffset after INC:%d",lseek(fd,0,SEEK_CUR));
    return ret;
}

bool fifo_ipc_init()
{
    int fd,ret;
    wl_buffer_init();
    g_fifo_mmap = NULL;
    fd = open(MMAP_FILE,O_CREAT|O_RDWR|O_TRUNC,00777);
    ret = write(fd,"",1);
    if(ret<0)
    {
        g_warning("Init fifoipc, fail to write %s",strerror(errno));
    }
    g_fifo_mmap = (char *)mmap(NULL,sizeof(char)*8,PROT_READ|PROT_WRITE,
                    MAP_SHARED,fd,0);
    close(fd);
    
    ret = (g_fifo_mmap!=NULL);
    if(!ret)
        g_warning("Fail to init fifo ipc");
    else{
        g_debug("Init fifo ipc OK");
        unmark_ssd_reading();
    }
    
    return ret;
}

bool fifo_ipc_finish()
{
    wl_buffer_finish();
    if(g_fifo_mmap)
    {
        g_debug("Finish fifo ipc");
        munmap(g_fifo_mmap,sizeof(char));
        g_fifo_mmap = NULL;
    }
}

bool inline is_write_paused(void)
{
    return g_fifo_mmap[0]==WRITE_SSD_PAUSED;
}

bool inline is_marked_reading(void)
{
    return g_fifo_mmap[0]==READING_SSD;
}

bool inline is_ssd_writing(void)
{
    bool ret = (g_fifo_mmap[0]==WRITING_SSD);
    //    g_debug("ISWRITING:%c",g_fifo_mmap[0]);
    return ret;
}

static bool inline notify_ssd_read(char s)
{
    if(g_fifo_mmap)
        g_fifo_mmap[0] = s;
}

bool inline mark_ssd_reading()
{
    //g_debug("MARK SSD READING >>");
    notify_ssd_read(READING_SSD);
}

bool inline unmark_ssd_reading()
{
    //g_debug("MARK SSD READING DONE <<<<");
    notify_ssd_read(DONE_READING_SSD);
}

static void
mapper_set_cache_dir(const char *path)
{
	int ret;
	struct stat st;

	cache_dir = strdup_chop_slash(path);

	ret = stat(cache_dir, &st);
	if (ret < 0)
		g_warning(">>>>failed to stat cache directory \"%s\": %s",
			  cache_dir, g_strerror(errno));
	else if (!S_ISDIR(st.st_mode))
		g_warning(">>>>cache directory is not a directory: \"%s\"",
			  cache_dir);
	g_debug("CACHE_DIR:%s",cache_dir);
}

//calculate hash for the cached file; use to read from cache
//NOTE!! when modify this function, make sure use the same hash algorithm with widea-mpd.
unsigned int getHash(char* str)
{
	unsigned int hash = 0;
	unsigned int i	  = 0;
	unsigned int len = strlen(str);
	for(i = 0; i < len; str++, i++)
	{
	   hash = (*str) + (hash << 6) + (hash << 16) - hash;
	}
	
	return hash;

}

void mapper_get_cache_hashuri(char* fullCachePath, char* cacheHashUri)
{
	unsigned int hash = getHash(fullCachePath);
	char *suffix = g_strrstr(fullCachePath,".");
	sprintf(cacheHashUri,"%s/%u%s",cache_dir,hash,suffix);
}

bool verifyCache(char *fileUrl,char *cacheUrl)
{
	struct stat fileStat,hashStat;	
	char hashUrl[MPD_PATH_MAX]={0};
	bool ret;
	if(g_str_has_prefix(fileUrl,music_dir))
	{
		fileUrl+=music_dir_length+1;
	}
#ifdef ORG
	struct song *song;
	song = db_get_song(fileUrl);
	if (song == NULL)
	{
//		g_debug("Verify cache: fail to get song [%s] from db",fileUrl);
		return false;
	}
	mapper_get_cache_hashuri(cacheUrl,hashUrl);
	if (stat(hashUrl, &hashStat)!=0) 
	{
//		g_debug("VerifyCache:Fail to stat file:%s:%s",hashUrl,g_strerror(errno));
		return false;
	}
	ret = (song->size == hashStat.st_size);		
//	g_debug("Verify Cache %s",ret==true?"OK":"NG");	//seems this line cause strange problem...
	return ret;
#else
    //// to do
    /// using /srv/widealab/aurender/aurender.db, we can check the file size
    /// instead of stat.
    /// In case of no data in aurender.db, it is a new file and we never played
    /// or updated, so we better delete it.
    ///
    ///
    
    __off64_t sizeFromDB = get_file_size_from_db(fileUrl);

    mapper_get_cache_hashuri(cacheUrl,hashUrl);
    if (stat(hashUrl, &hashStat)!=0) {
        g_debug("VerifyCache:Fail to stat file:%s:%s",hashUrl,g_strerror(errno));
        return false;
    }
    else {

		//'15.09.15 Prevent Underrun from au_pl_task
		if(au_pl_task_active == 0)
		{
	        g_message("Cache check for %s", fileUrl);
	        g_message("      fileSize in db     %lld", sizeFromDB);
	        g_message("      fileSize in cache  %lld", hashStat.st_size);
		}

        if (hashStat.st_size != sizeFromDB) {
              //// since file changed we need to cache again.
            g_message("the file size does not match with cache and db for %s", fileUrl);
              ///deleteCache(song_get_uri(song));
            if(sizeFromDB==-1)
            {
            	char *diskFile = g_strconcat("/hdds/",fileUrl, NULL);
				if(access(diskFile, F_OK) != 0) 
				{
					if( hashStat.st_size > 0)
					{
						g_message("Play USB/NAS cached file\n");
						return true;
					}
					else
					{
						g_message("Play Stream file\n");
						return false;
					
					}
				}
				
				lstat(diskFile, &fileStat);
				g_message("      fileSize in disk  %lld", fileStat.st_size);		
				g_free(diskFile);
				if(hashStat.st_size == fileStat.st_size )
				{
	            	g_message("Play with cached file\n");
					return true;
				}
				else
				{
	            	g_message("Play with original file\n");
					return false;
				
				}
            }
            return false;
        }
    }
    return true;
#endif
}

bool isCacheOK(char* fullCachePath)
{
	char hashUri[MPD_PATH_MAX]={0};
        if(is_nas_lnk(fullCachePath))
                return false;

	mapper_get_cache_hashuri(fullCachePath,hashUri);
	return isRegularAndReadable(hashUri);
}

/*
music dir /a/b
cache dir /m/n
in: full path of audio file like /a/b/c/d/e.mp3
out: /m/n/c/d/e.mp3
*/
void mapper_get_cache_url(char *fileUrl,char *cacheUrl)
{
    if(g_str_has_prefix(fileUrl,music_dir))
	{
		sprintf(cacheUrl,"%s%s",cache_dir,fileUrl+music_dir_length);
  	}
  else
  	{
    	sprintf(cacheUrl,"%s/%s",cache_dir,fileUrl);
  	}
}

//when the path is not a full path.
void mapper_get_cache_url_1(char *fileUrl,char *cacheUrl)
{
	sprintf(cacheUrl,"%s/%s",cache_dir,fileUrl);
}

bool isStateFileRegularAndReadable(char *path)
{
    char fullpath[PATH_MAX]={0};
    sprintf(fullpath,"%s/%s",music_dir,path);
    //g_debug("FULLPATH:%s",fullpath);
    bool ret = isRegularAndReadable(fullpath);
    //g_debug("File is %s normal",ret?"":"NOT");
    return ret;
}

bool isRegularAndReadable(char* path)
{
  struct stat     statbuf;
  bool ret=false;
  //g_message(" stat for ----> %s", path);
  if (lstat(path, &statbuf) < 0) /* stat error */
    {
//      g_debug("Cannot stat:%s:%s,\n",path,strerror(errno));
      return ret;
    }
  if (S_ISREG(statbuf.st_mode)) /* is a regular file */
    {
      if (access(path, R_OK) < 0)
	return ret;
      else
	{
	  ret = true;
	}
    }
  return ret;
}

int pipe_fd=-1;

static void render_pipe_init()
{
  int ret;
  
  ret = mkfifo(PIPE_PATH,S_IRWXU|S_IRWXG|S_IROTH);
  if(ret<0)
  {
    //g_debug("Fail to Create render_pipe,%s",strerror(errno));
  }  
  pipe_fd = open(PIPE_PATH,O_RDWR|O_NONBLOCK);

  if(pipe_fd<0)
  {
  	g_debug("Fail to Open render_pipe,%s",strerror(errno));
  }
  else
  {
    g_debug("Open render_pipe,%s",strerror(errno));
  }
}

static void render_pipe_finish()
{
  if(pipe_fd>0)
    {
      close(pipe_fd);
      pipe_fd = -1;
    }
}

struct sockaddr_in gUdpServer,gUdpMeterServer;
int gUdpSocket,gUdpMeterSocket;

static void blaster_client_init()
{
	int returnStatus;
	int addrlen;
	struct sockaddr_in udpClient;
	gUdpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (gUdpSocket == -1)
	{
	g_debug("Could not create a socket!\n");
	return;
	}
	else 
	{
	g_debug("Socket created.\n");
	}
	/* client address */
	/* use INADDR_ANY to use all local addresses */
	udpClient.sin_family = AF_INET;
	udpClient.sin_addr.s_addr = INADDR_ANY;
	udpClient.sin_port = 0;
	returnStatus = bind(gUdpSocket, (struct sockaddr*)&udpClient,
	sizeof(udpClient));
	if (returnStatus == 0) 
	{
		g_debug("Bind completed!\n");
	}
	else 
	{
	g_debug("Could not bind to address!\n");
	close(gUdpSocket);
	return;
	}
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
	gUdpServer.sin_family = AF_INET;
	gUdpServer.sin_addr.s_addr = inet_addr("127.0.0.1");
	gUdpServer.sin_port = htons(6265);
        returnStatus = setsockopt(gUdpSocket,SOL_SOCKET,SO_RCVTIMEO,(char *)&tv,sizeof(struct timeval));
        g_warning("init blaster socket:%s",strerror(errno));
}

static void blaster_client_finish()
{
	if(gUdpSocket>0)
		close(gUdpSocket);
}

static bool sendBufferToBlaster(char *buf)
{
	// is a mutex necessary here?
	int returnStatus = sendto(gUdpSocket, buf, strlen(buf)+1, 0,
					(struct sockaddr*)&gUdpServer, sizeof(gUdpServer));
	if(returnStatus<0)
	{
		g_debug("Fail to send Buffer : %s",buf);
		return false;
	}
	else
		g_debug("Send:: [%s]",buf);
	return true;
}

static void meter_client_init()
{
	int returnStatus;
	struct sockaddr_in udpClient;
	gUdpMeterSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (gUdpMeterSocket == -1)
	{
	g_debug("Could not create a socket!\n");
	return;
	}
	else 
	{
	g_debug("Socket created.\n");
	}
	/* client address */
	/* use INADDR_ANY to use all local addresses */
	udpClient.sin_family = AF_INET;
	udpClient.sin_addr.s_addr = INADDR_ANY;
	udpClient.sin_port = 0;
	returnStatus = bind(gUdpMeterSocket, (struct sockaddr*)&udpClient,
	sizeof(udpClient));
	if (returnStatus == 0) 
	{
		g_debug("Bind completed!\n");
	}
	else 
	{
	g_debug("Could not bind to address!\n");
	close(gUdpMeterSocket);
	return;
	}
	gUdpMeterServer.sin_family = AF_INET;
	gUdpMeterServer.sin_addr.s_addr = inet_addr("127.0.0.1");
}

static void meter_client_finish()
{
	if(gUdpMeterSocket>0)
		close(gUdpMeterSocket);
}

bool writeToFile(const char *filePath,char *buffer,int size,bool append)
{
	bool ret = true;
	int to = 0;
	if(append)
		to = open(filePath,O_WRONLY | O_CREAT | O_APPEND,S_IRWXU | S_IRWXG | S_IROTH);
	else
		to = open(filePath,O_WRONLY | O_CREAT | O_TRUNC,S_IRWXU | S_IRWXG | S_IROTH);  

	if(to<0)
	{
		g_debug("Cannot open file %s : %s\n",filePath,strerror(errno));
		ret =  false;goto out;
	}	  
	int nWrite;
	if(append)
		nWrite = write(to,"\n",1);
	nWrite = write(to,buffer,size);

out:
	if(to>0)
		close(to);	  
	return ret;
}

static bool sendBufferToAuMeter(char *buf)
{
	// is a mutex necessary here?
	int returnStatus;
	gUdpMeterServer.sin_port = htons(7910);
	returnStatus = sendto(gUdpMeterSocket, buf, strlen(buf)+1, 0,
					(struct sockaddr*)&gUdpMeterServer, sizeof(gUdpMeterServer));
	gUdpMeterServer.sin_port = htons(7911);
	returnStatus = sendto(gUdpMeterSocket, buf, strlen(buf)+1, 0,
					(struct sockaddr*)&gUdpMeterServer, sizeof(gUdpMeterServer));
	if(returnStatus<0)
	{
		g_debug("Fail to send Buffer to Meter : %s",buf);
		return false;
	}
	else
		g_debug("Send to Meter:: [%s]",buf);
//	const bwFile="/home/widealab/.config/Widealab/.bw";
	const bwFile="/tmp/.bw";	
	writeToFile(bwFile,buf,strlen(buf),false);
	return true;
}

bool send_volume_cmd(int volume)
{
	char buf[3]={0};
	sprintf(buf,"t%d%d",volume>0?0:1,volume>0?volume:volume*(-1));		
	return sendBufferToBlaster(buf);
}

bool send_wakeup_cmd()
{
	char buf[3]="Y1";
	return sendBufferToBlaster(buf);
}
/*
  return true when audioMode is ON mode (file detected)
  false when OFF mode
  By default it is OFF, so there's no flag file
*/
bool is_audio_mode_enabled()
{
    int file = 0;
    file = open(AM_FLAG,O_RDONLY);
    if (file>0) {
        close(file);
        return true;
    }
    return false;
}

/*
  return true when audioMode is ON mode (file detected)
  false when OFF mode
  By default it is OFF, so there's no flag file
*/
bool is_fading_enabled()
{
    int file = 0;
    file = open("/srv/widealab/pcSet",O_RDONLY);
    if (file>0) {
        close(file);
        return true;
    }
    return false;
}

bool isCopying()
{
	int ret;
	struct stat st;

	ret = stat("/srv/copysrv/.copy_server_copying", &st);
	if (ret > -1) {
        g_warning("[Copying]Found it is copying, so we skip playing");
        return true ;
    }
	else {
	    ret = stat("/srv/copysrv/.copy_server_completed", &st);

        if (ret > -1) {
            g_warning("[Copying]Let's remove the copying done.");
            system("/usr/local/sbin/remove_copy_completed &");
        }
        return false;
    }
}


inline bool is_wakeup_cmd(char *cmd)
{
    //g_debug("CMD:%s",cmd);
    if(strcmp(cmd,"status")==0
       ||strcmp(cmd,"idle")==0
       ||strcmp(cmd,"stats")==0
       ||strcmp(cmd,"update")==0
       ||strcmp(cmd,"setvol")==0
       ||strcmp(cmd,"notcommands")==0
       ||strcmp(cmd,"commands")==0
       ||strcmp(cmd,"output")==0
       ||strcmp(cmd,"outputs")==0
       ||strcmp(cmd,"tagtypes")==0
       ||strcmp(cmd,"currentsong")==0
       ||strcmp(cmd,"playlistid")==0
       ||strcmp(cmd,"close")==0
       ||strcmp(cmd,"seekid")==0
       ||strcmp(cmd,"playlist")==0
       ||strcmp(cmd,"plchangeswl")==0
       ||strcmp(cmd,"password")==0
       ||strcmp(cmd,"listplaylists")==0
       ||strcmp(cmd,"crossfade")==0
       ||strcmp(cmd,"add")==0
       ||strcmp(cmd,"playlistadd")==0
       ||strcmp(cmd,"addid")==0)
        return false;
    //when in audioMode, only below cmd is trated as wakeup cmd
    if(is_audio_mode_enabled())
    {
        if(strcmp(cmd,"play")==0
           ||strcmp(cmd,"playid")==0
           ||strcmp(cmd,"next")==0
           ||strcmp(cmd,"prev")==0
           ||strcmp(cmd,"pause")==0)
            {
                g_debug("Wake for audio");
                return true;
            }
        else
            return false;
    }
    //when not in audio mode, other cmds like delete, is a wakeup cmd
    return true;
}

void send_stop_cmd()
{
    //blaster will take care of stop cmd it self.
    /**/
    int returnStatus;
    char buf[32]="e";
    sendBufferToBlaster(buf);
    system("touch /tmp/.blasterstop");     
}

void send_mute_cmd()
{
    int returnStatus;
    char buf[32]="q1";
    sendBufferToBlaster(buf);  
    clear_sample_rate();
    g_debug("----Mute & Clean SampleRate----");
}
void send_unmute_cmd()
{
    int returnStatus;
    char buf[32]="q0";
    sendBufferToBlaster(buf);    
}

/*
  means the flag file for self audio mode on is detected
  in this case we will not going to audio mode when start to play
  anyone set the flag should go audio mode himself
  but we will remove the flag file once we skip it.
*/
bool is_selfon_audio_mode()
{
    int file = 0;
    file = open(SELF_AM_FLAG,O_RDONLY);
    if (file>0) {
        close(file);
        remove(SELF_AM_FLAG);
        return true;
    }
    return false;
}


bool is_auto_dim_enabled()
{
    int file = 0;
    file = open(AP_FLAG,O_RDONLY);
    if (file>0) {
        close(file);
        return true;
    }
    return false;
}

//request = '0' or '1'
void update_audio_mode_from_blaster(int request)
{
	int file = 0;
	char buffer[10]={0};
	int ret = 0;
	int am = 0;
	file = open(AM_FORMPD,O_RDONLY);
	if (file>0) {
		if (read(file,buffer,1)>0) {
			am = buffer[0];
		}
		close(file);
	}
	if(am!=0)
	{
            g_debug("AM::%d(blaster) vs %d(request)",am,request);
            if(am!=request)
                {
                    g_debug("Not match, restore to 0");
                    g_audio_mode = 0;
                }
            else
                {
                    g_debug("Match");
                    g_audio_mode = request;
                }
        }
	else
	{
		g_audio_mode = 0;
		g_debug("Fail to get AM from blaster, restore to 0");
	}
    
}

void cancel_atcmd()
{
    system("/usr/local/sbin/wlkall amdelay &");
}

//update the operation stamp when user issued one
void update_opstamp()
{
    // now even protection timer is disabled, we will dim to 20 after 3mins, so we need to update it.
    //    if(is_auto_dim_enabled()){
    system("/usr/local/sbin/updateOpStamp &");
    //    }
}

inline void AMOLED_on_delay_off_for_normal() {
	g_debug("<<<<<<<<<<<AMOLED_on_delay_off_normal>>>>>>>>>>>>>>");
    system("/usr/local/sbin/showAndHideAMOLED &");
}

void AMOLED_on_delay_off()
{
	g_debug(">>>>>>>>>>AMOLED_on_delay_off<<<<<<<<<<<<<<");
    cancel_atcmd();
    usleep(100000);
    send_at1_cmd();
    send_at0_cmd();
}

//@1 command with 1sec delay to avoid blink
void send_atcmd_with_delay(char *c,int n)
{
    char cmd[256]={0};
    sprintf(cmd,"/usr/local/sbin/amdelay %s %d &",c,n);
    //g_debug("CMD:%s",cmd);
    system(cmd);
}

// audio mode on
bool send_at0_cmd()
{
    char buf[3]="@0";
    send_atcmd_with_delay(buf,6);
    update_opstamp(); //reset protection timer
    return true;
}

// audio mode off
bool send_at1_cmd()
{
    //if(g_audio_mode == '1')
    //    return true;
    char buf[3]="@1";
    sendBufferToBlaster(buf);
    update_opstamp(); //reset protection timer
    return true;
}

int get_blaster_sr()
{
    int file = 0;
    char buffer[10]={0};
    int ret = 0;
    int rate = 0;
    file = open(SR_FORMPD,O_RDONLY);
    if (file>0) {
        if (read(file,buffer,1)>0) {
            ret = buffer[0];
        }
        close(file);
    }
    switch(ret)
	{
        case '0':
            rate = 44100;
            break;
        case '1':
            rate = 88200;
            break;
        case '2':
            rate = 176400;
            break;
        case '3':
            rate = 48000;
            break;
        case '4':
            rate = 96000;
            break;
        case '5':
            rate = 192000;
            break;
        case '6':
            rate = 32000;
            break;
        case '7':
            rate = 64000;
            break;
        case '8':
            rate = 352800;
            break;
        case '9':
            rate = 384000;
            break;
	}
    if(ret!=0)
	{
            return rate;
        }
    return 0;
}

void update_sample_rate_from_balster(int request)
{
	int file = 0;
	char buffer[10]={0};
	int ret = 0;
	int rate = 0;
	file = open(SR_FORMPD,O_RDONLY);
	if (file>0) {
		if (read(file,buffer,1)>0) {
			ret = buffer[0];
		}
		close(file);
	}
	switch(ret)
	{
		case '0':
			rate = 44100;
			break;
		case '1':
			rate = 88200;
			break;
		case '2':
			rate = 176400;
			break;
		case '3':
			rate = 48000;
			break;
		case '4':
			rate = 96000;
			break;
		case '5':
			rate = 192000;
			break;
		case '6':
			rate = 32000;
			break;
		case '7':
			rate = 64000;
			break;			
        case '8':
            rate = 352800;
            break;
        case '9':
            rate = 384000;
            break;
	}
	if(ret!=0)
	{
        g_debug("%d(blaster) vs %d(request)",rate,request);
        if(rate!=request)
        {
            g_debug("Not match, restore to 0");
            g_sample_rate = 0;
        }
        else
        {
            g_debug("Match");
            g_sample_rate = request;
        }
    }
	else
	{
		g_sample_rate = 0;
		g_debug("Fail to get SR from blaster, restore to 0");
	}
    
}

bool checkBlasterResponse()
{
    int ret = 0;
    int addrLen = 0;
    char buf[32] = {0};
    memset(buf,0,sizeof(buf));
    addrLen = sizeof(gUdpServer);
    g_debug("Waiting blaster response");
    ret = recvfrom(gUdpSocket,buf,sizeof(buf),0,(struct sockaddr*)&gUdpServer, &addrLen);
    if(ret<0)
        g_warning("Fail to receive blaster response:%s",strerror(errno));
    else{
        g_debug("blaster: %s",buf);
        return true;
    }
    return false;
}

void get_sample_rate_from_balster()
{
	int file = 0;
	char buffer[1024]={0};
	int ret = 0;
	int rate = 0;
	file = open(SR_FORMPD,O_RDONLY);
	if (file>0) {
		if (read(file,buffer,1)>0) {
			ret = buffer[0];
		}
		close(file);
	}
	switch(ret)
	{
		case '0':
			rate = 44100;
			break;
		case '1':
			rate = 88200;
			break;
		case '2':
			rate = 176400;
			break;
		case '3':
			rate = 48000;
			break;
		case '4':
			rate = 96000;
			break;
		case '5':
			rate = 192000;
			break;
		case '6':
			rate = 32000;
			break;
		case '7':
			rate = 64000;
			break;
        case '8':
            rate = 352800;
            break;
        case '9':
            rate = 384000;
            break;
	}
	if(ret!=0)
	{
	  g_sample_rate = rate;
	}
	else
	{
	  g_sample_rate = 0;
	  g_debug("Fail to get SR from blaster, restore to 0");
	}
}

void clear_sample_rate()
{	
	g_sample_rate = 0;
}

void send_Y2_cmd()
{    
	sendBufferToBlaster("Y2");
}

int getPauseTime()
{
  int file = 0;
  char buffer[1024]={0};
  int ret = 0;
  file = open(PAUSE_INTERVAL,O_RDONLY);
  if (file>0) {
    if (read(file,buffer,2)>0) {
	  if (buffer[1] != '\0') {
        ret = (buffer[0]-'0') * 10;
        ret += buffer[1]-'0';
      }
	  else
        ret = buffer[0]-'0';
    }
    close(file);
  }
  g_message("PauseInterval from file:%s",buffer);
  g_debug("PauseInterval from file:%s",buffer);
  return ret>0?ret:0;
}

static inline bool is_supported_sr(int sr)
{
  return (sr==44100||sr==88200||sr==176400||sr==352800||sr==48000
	  ||sr==96000||sr==192000||sr==384000||sr==32000||sr==64000);
}

bool isRecentTouched(char *file)
{
    struct stat statbuf;
    g_message(" stat for 2 -----> %s", file);
    if (stat(file, &statbuf) == 0)
    {
        //compare the time stamp
        time_t ltime;
        ltime=time(NULL);
        if(ltime-2<statbuf.st_ctime) //if flag updated within 2s
            return true;
    }
    return false;
}

/*
  change from detect file exsitence to file recent change
 */
bool isBlasterJustStopped()
{
    return isRecentTouched("/tmp/.blasterstop");
}
bool isBlasterStopped()
{
    return isRegularAndReadable("/tmp/.blasterstop");
}

bool send_blaster_stop_cmd(int sample_rate, int isDSD)
{
    int returnStatus;
    int interval;
    char buf[32]={0};
    char rate=0;
    char resumeCmd[256]={0};

	// IF dsd2pcm enabled and select samplrate to 88.2Khz instead of 176.4Khz, then force blaster to select 88.2Khz.

	if(isDSD==1)
	{
		if(dsd_pcm_mode==0 && sample_rate == 176400)
		{
			sample_rate = 88200;
			
		    if(g_sample_rate==88200)
	    	    return true;	
			g_message("[send_blaster_stop_cmd] Force blaster to set 88.1Khz");
		}      
		
		if(dsd_pcm_mode==1 && sample_rate == 176400)
		{
		    if(g_sample_rate==176400)
	    	    return true;	
		}    
	}

    if(is_supported_sr(sample_rate)==false)
        sample_rate = 44100;
    if(g_sample_rate == 0)
    {
        //if blaster's sr matches, then we will skip e cmd
        //g_sample_rate = get_blaster_sr(); but not set g_sample_rate otherwise it would cause no sound after close audio, should update it with s cmd
        if(get_blaster_sr()==sample_rate)
            {
                return true;
            }
    }
    if(g_sample_rate==sample_rate)
        return true;

    if(isBlasterJustStopped()) 
        return true;
    g_debug(">>>>>>>>>SR mismatch:%d vs %d:send stop cmd",g_sample_rate,sample_rate);
    int i = 0;
    for(i = 0;i<1;i++)
    {
        send_stop_cmd();
        usleep(100000);
        if(isBlasterStopped()) 
	  {
	    g_debug("Blaster audio stop flagged");
	    return true;
	  }
        //if(checkBlasterResponse())
        //    return true;
    }
    return true;
    //return false;
}

bool isW20()
{
    return isRegularAndReadable("/home/widealab/.config/model.w20");
}

bool cacheDisabled()
{
    return isRegularAndReadable("/srv/widealab/disableCache");
}

bool send_sample_rate_cmd_and_pause(struct player_control *pc,bool *paused,int sample_rate, int isDSD)
{
	int returnStatus;
        int interval;
	char buf[32]={0};
	char rate=0;
	char resumeCmd[256]={0};

	// IF dsd2pcm enabled and select samplrate to 88.2Khz instead of 176.4Khz, then force blaster to select 88.2Khz.
	if(isDSD==1)
	{
		if(dsd_pcm_mode==0 && sample_rate == 176400)
		{
			sample_rate = 88200;
			
		    if(g_sample_rate==88200)
	    	    return true;	
			g_message("[send_sample_rate_cmd_and_pause] Force blaster to set 88.1Khz");
		}      
		
		if(dsd_pcm_mode==1 && sample_rate == 176400)
		{
		    if(g_sample_rate==176400)
	    	    return true;	
		}    
	}

	
	if(is_supported_sr(sample_rate)==false)
	   sample_rate = 44100;
	
	if(g_sample_rate==sample_rate) {
       // g_message("Blaster ---> same to old %ld, so skip", sample_rate);
		return true;
    }
	//update_sample_rate_from_balster(sample_rate);
        int i = 0;
        int sr = 0;
        bool fromPause = (g_sample_rate == 0);
        if(fromPause)
        {
            sr = get_blaster_sr();
        }
	switch(sample_rate)
		{
			case 44100:
				rate = '0';
				break;
			case 88200:
				rate = '1';
				break;
			case 176400:
				rate = '2';
				break;
			case 352800:
				rate = '8';
				break;
			case 48000:
				rate = '3';
				break;
			case 96000:
				rate = '4';
				break;
			case 192000:
				rate = '5';
				break;
			case 384000:
				rate = '9';
				break;
			case 32000:
				rate = '6';
				break;
			case 64000:
				rate = '7';
				break;			
		}
	sprintf(buf,"s%c",rate);
    g_message("Blaster ---> got %ld and send to blaster[%s]", sample_rate, buf);

#if 0
        if(g_is_w20)
        {
            //handle w20 differently:
            //1. pause player and wait for s cmd to finish, then resume by script
            //2. skip pause interval because the sr cmd generally took 15s or more.
	  if(get_blaster_sr()!=sample_rate)
	    {
	      sendBufferToBlaster(buf);
	      g_debug(">>PAUSING for rate change");
	      audio_output_all_pause_no_clear(); 
	      player_lock(pc);
	      pc->state = PLAYER_STATE_PAUSE;
	      *paused = true;
              g_changing_rate = 1;              
	      player_unlock(pc);
	      sprintf(resumeCmd,"/usr/local/sbin/resumeAtRate %c >> /tmp/.resumeatratelog &",rate);
	      g_debug("Will resume play by:%s",resumeCmd);
	      system(resumeCmd);
	      memset(resumeCmd,0,sizeof(resumeCmd));
	    }
          else
            {
                g_changing_rate = 0;
                if(isBlasterStopped())  //when rate matches but audio stopped
                    {
                        g_debug(">>Wake up from stop");
                        sendBufferToBlaster("z");
                        system("rm -f /tmp/.blasterstop");
                        g_debug(">>Clear blaster stop flag");
                    }
            }
        }
        else
#endif
        if(get_blaster_sr()!=sample_rate)
        {
            for(i = 0;i<1;i++)
                {
                    sendBufferToBlaster(buf);
                    g_debug("Wait blaster response for rate command #%d",i);
                    if(checkBlasterResponse())
                        {
                            g_sample_rate = sample_rate;
                            break;
                        }
                }
            /*
              if(i==5)
                {
                    g_warning("Fail to send rate cmd");
                    return false;
                }
            */
        }
        else
        {
            g_changing_rate = 0;
            if(isBlasterStopped())  //when rate matches but audio stopped
                {
                    g_debug(">>Wake up from stop");
                    sendBufferToBlaster("z");
                    system("rm -f /tmp/.blasterstop");
                    g_debug(">>Clear blaster stop flag");
                }
            else
                g_sample_rate = sample_rate;
        }

        //when resume from pause, if blaster's sample rate is what we need,
        //then we will only send the s cmd(to wake up from e cmd) but will not pause.
        if(sr != sample_rate)
        {
            interval = getPauseTime();
            if(interval>0)
                {
                    g_debug(">>PAUSING for interval:%d",interval);
                    //system("mpc --port 12019 pause&");
                    audio_output_all_pause_no_clear(); //pause all output to avoid stutter
                    player_lock(pc);
                    pc->state = PLAYER_STATE_PAUSE;
                    *paused = true;
                    player_unlock(pc);
                    sprintf(resumeCmd,"sleep %d&&mpc --port 12019 play &",interval);
                    g_debug("Will resume play by:%s",resumeCmd);
                    system(resumeCmd);
                }
            else 
            {
                //// to turn on and off the screen
                AMOLED_on_delay_off_for_normal();
            }
        }
        else
        {
            g_debug("Rate matches, ignore pause interval :%d | %d",sr,sample_rate);
            //// to turn on and off the screen
            AMOLED_on_delay_off_for_normal();
        }
        return true;
}

#if 0
//check sr file when sample rate doesnot match;
//set gsample rate after sr file match sample rate
void send_sample_rate_cmd(int sample_rate)
{
	int returnStatus;
	char buf[32]={0};
	char rate=0;
	int interval;
	if(g_sample_rate==sample_rate)
		return;
//	update_sample_rate_from_balster();
//	send_stop_cmd();
	switch(sample_rate)
		{
			case 44100:
				rate = '0';
				break;
			case 88200:
				rate = '1';
				break;
			case 176400:
				rate = '2';
				break;
			case 48000:
				rate = '3';
				break;
			case 96000:
				rate = '4';
				break;
			case 192000:
				rate = '5';
				break;
			case 32000:
				rate = '6';
				break;
			case 64000:
				rate = '7';
				break;			
		}
	sprintf(buf,"s%c",rate);
	interval = getPauseTime()*10;
	sendBufferToBlaster(buf);
	usleep(1000);
	update_sample_rate_from_balster(sample_rate);
	while(--interval>0)
	  {
	    g_debug("Pausing:%d",interval);
	    usleep(100000);
	    if(g_sample_rate!=sample_rate)
	      {
		update_sample_rate_from_balster(sample_rate);
		sendBufferToBlaster(buf);
	      }
	  }
}
#endif
void send_bit_width(int f)
{
// TODO: should also save this to a file, for meter init;
	int returnStatus;
	char buf[32]={0};
	int bw=0;
	switch(f)
		{
			case SAMPLE_FORMAT_S24:
			case SAMPLE_FORMAT_S24_P32:
			case SAMPLE_FORMAT_S32:
				bw = 24;
				break;
			default:
				bw = 16;
				break;
		}
	sprintf(buf,"%d",bw);
	sendBufferToAuMeter(buf);
}

void cacheExisted(char *cacheUrl)
{
  write_render_pipe(cacheUrl,FIFO_UPDATE);
}
void cacheNotExisted(char *cacheUrl)
{
  write_render_pipe(cacheUrl,FIFO_ADD);
}

void cacheDeleteForever(char *cacheUrl)
{
  g_debug("Cache delete file file:%s",cacheUrl);
  write_render_pipe(cacheUrl, FIFO_DELETE_FOREVER);
}
void write_render_pipe(char *cacheUri,int msg)
{
  //int len = strlen(cacheUri)+2;
  static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
  char buff[MPD_PATH_MAX+32]={0};  
  if(is_nas_lnk(cacheUri))
  {
      g_debug("Skip write pipe for nas or TIDAL file:%s",cacheUri);
      return;
  }
  g_static_mutex_lock (&mutex);
  char *suffix = g_strrstr(cacheUri,".");
  sprintf(buff,"<%s?%u%s?%d>",cacheUri,getHash(cacheUri),suffix,msg);
  g_debug("WritePipe:[ \"%s\"]",buff);
  write(pipe_fd,buff,strlen(buff));
  g_static_mutex_unlock (&mutex);
}

//will check the file existance and send add/update msg
//used when load/restore pl or add new songs to pl
void addToCache(char *uri)
{
    if(g_cache_disabled)
        return;
	char cacheUri[MPD_PATH_MAX]={0};
	mapper_get_cache_url(uri,cacheUri);
	if(isCacheOK(cacheUri)&&(verifyCache(uri,cacheUri)==true))
		{
			//cacheExisted(cacheUri);
			//do nothing, update when we play it
		}
	else
		{
			cacheNotExisted(cacheUri);
		}
}

//when the song is removed from pl, we send a msg,
//let the fifoManager decide what to do, or just do nothing.
void deleteCache(char *uri)
{
    if(g_cache_disabled)
        return;
	char cacheUri[MPD_PATH_MAX]={0};
	mapper_get_cache_url(uri,cacheUri);
	write_render_pipe(cacheUri,FIFO_DELETED);
}

void restartAirport()
{
    int ret = 0;
    if(isRegularAndReadable("/tmp/.airplaying"))
        {
            g_debug("Airplaying, restart airport daemon now");
            ret = system("/usr/local/sbin/wlkall dns-sd & kill -9 `ps awx|grep  \'/usr/local/bin/shairport\' |grep -v \'grep\'|awk \'{print $1}\'`;rm -f /tmp/.airplaying & /usr/local/bin/auAirPort &");
        }
}

#endif

void mapper_init(const char *_music_dir, const char *_playlist_dir)
{
  GError *error = NULL;
	if (_music_dir != NULL)
		mapper_set_music_dir(_music_dir);

	if (_playlist_dir != NULL)
		mapper_set_playlist_dir(_playlist_dir);
#ifdef SSD_CACHE
	//const char *path;
        //path = config_dup_path(CONF_CACHE_DIR,&error);
        //if (path != NULL)
        g_is_w20 = isW20();
        g_cache_disabled = cacheDisabled();
        g_message("SSD Cache %s",g_cache_disabled?"DISABLED":"ENABLED");
        if(!g_cache_disabled){
            int ret=system("mount|grep /cache -iq");
            if(ret == 0)
                mapper_set_cache_dir("/cache");
            else
                mapper_set_cache_dir("/srv/cache");
            render_pipe_init();
        }
	blaster_client_init();
	meter_client_init();
#endif
}

void mapper_finish(void)
{
	g_free(music_dir);
	g_free(playlist_dir);
#ifdef SSD_CACHE
        save_tempdb();
	g_free(cache_dir);
	render_pipe_finish();
	blaster_client_finish();
	meter_client_finish();
#endif
}

bool
mapper_has_music_directory(void)
{
	return music_dir != NULL;
}

const char *
map_to_relative_path(const char *path_utf8)
{
	return music_dir != NULL &&
		memcmp(path_utf8, music_dir, music_dir_length) == 0 &&
		G_IS_DIR_SEPARATOR(path_utf8[music_dir_length])
		? path_utf8 + music_dir_length + 1
		: path_utf8;
}

char *
map_uri_fs(const char *uri)
{
	char *uri_fs, *path_fs;

	assert(uri != NULL);
	assert(*uri != '/');

	if (music_dir == NULL)
		return NULL;

	uri_fs = utf8_to_fs_charset(uri);
	if (uri_fs == NULL)
		return NULL;

	path_fs = g_build_filename(music_dir, uri_fs, NULL);
	g_free(uri_fs);

	return path_fs;
}

char *
map_directory_fs(const struct directory *directory)
{
	assert(music_dir != NULL);

	if (directory_is_root(directory))
		return g_strdup(music_dir);

	return map_uri_fs(directory_get_path(directory));
}

char *
map_directory_child_fs(const struct directory *directory, const char *name)
{
	char *name_fs, *parent_fs, *path;

	assert(music_dir != NULL);

	/* check for invalid or unauthorized base names */
	if (*name == 0 || strchr(name, '/') != NULL ||
	    strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return NULL;

	parent_fs = map_directory_fs(directory);
	if (parent_fs == NULL)
		return NULL;

	name_fs = utf8_to_fs_charset(name);
	if (name_fs == NULL) {
		g_free(parent_fs);
		return NULL;
	}

	path = g_build_filename(parent_fs, name_fs, NULL);
	g_free(parent_fs);
	g_free(name_fs);

	return path;
}

char *
map_song_fs(const struct song *song)
{
	assert(song_is_file(song));

	if (song_in_database(song))
		return map_directory_child_fs(song->parent, song->uri);
	else
		return utf8_to_fs_charset(song->uri);
}

char *
map_fs_to_utf8(const char *path_fs)
{
	if (music_dir != NULL &&
	    strncmp(path_fs, music_dir, music_dir_length) == 0 &&
	    G_IS_DIR_SEPARATOR(path_fs[music_dir_length]))
		/* remove musicDir prefix */
		path_fs += music_dir_length + 1;
	else if (G_IS_DIR_SEPARATOR(path_fs[0]))
		/* not within musicDir */
		return NULL;

	while (path_fs[0] == G_DIR_SEPARATOR)
		++path_fs;

	return fs_charset_to_utf8(path_fs);
}

const char *
map_spl_path(void)
{
	return playlist_dir;
}

char *
map_spl_utf8_to_fs(const char *name)
{
	char *filename_utf8, *filename_fs, *path;

	if (playlist_dir == NULL)
		return NULL;

	filename_utf8 = g_strconcat(name, PLAYLIST_FILE_SUFFIX, NULL);
	filename_fs = utf8_to_fs_charset(filename_utf8);
	g_free(filename_utf8);
	if (filename_fs == NULL)
		return NULL;

	path = g_build_filename(playlist_dir, filename_fs, NULL);
	g_free(filename_fs);

	return path;
}

__off64_t
get_file_size_from_db(const char* path)
{
    const char* actualPath = map_to_relative_path(path);

    static sqlite3 *   db;
    static const char* dbPath = "/srv/widealab/aurender/aurender.db";
    static sqlite3_stmt *const stmt;
    static const char* sql = "select size from track_meta "
            " where track_id = (select track_id from tracks "
            " where fileName = ? )";
    int ret;
    static GError **error_r;
    __off64_t size = -1;

    ret = sqlite3_open(dbPath, &db);
    if (ret != SQLITE_OK) {
        g_set_error(error_r, g_quark_from_static_string("aurenderDB"), ret,
                "Failed to open sqlite database '%s': %s",
                dbPath, sqlite3_errmsg(db));
    }
    else {

        //sqlite3_reset(stmt);
        ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (ret != SQLITE_OK) {
            g_warning("failed to bind path for file size: %s",
                  sqlite3_errmsg(db));
        }
        else {
            ret = sqlite3_bind_text(stmt, 1, actualPath, -1, NULL);
            if (ret != SQLITE_OK) {
                g_warning("failed to bind path for file size: %s",
                      sqlite3_errmsg(db));
            }
            else {

                do {
                    ret = sqlite3_step(stmt);
                } while (ret == SQLITE_BUSY);

                if (ret == SQLITE_ROW) {
                    /* record found */
                    size = sqlite3_column_int64(stmt, 0);
                } else if (ret == SQLITE_DONE) {
                    /* no record found */
                } else {
                    /* error */
                    g_warning("sqlite3_step() failed: %s",
                          sqlite3_errmsg(db));
                }
                sqlite3_reset(stmt);
            }

            sqlite3_clear_bindings(stmt);

            sqlite3_finalize(stmt);
        }

        sqlite3_close(db);
    }

    return size;
}
