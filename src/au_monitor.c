#include <glib.h>
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>


#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

#include <stdlib.h>

#include "playlist.h"
#include "player_control.h"
#include "playlist_error.h"
#include "main.h"


#include "mapper.h"

int dsd_pcm_mode=-1; // 0 for 88.2 Khz, 1 for 176.4 Khz

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "au_monitor"

static GThread *au_monitor_thr = NULL;

int parse_dsd2pcm_enable(void)
{
	char buf[4];
	int len;
	struct stat st={0};

	int fd = open("/etc/blaster/dsd2pcm_enable", O_RDONLY);

	if( fd < 0 )
	{
		g_message("[parse_dsd2pcm_enable] file open fail\n");
		return -1;
	}

	fstat(fd, &st);
	
	len = read(fd, buf, 4);
	
	if(len<0) 
	{
		close(fd);
		g_message("[parse_dsd2pcm_enable] file [%d] open fail1\n", fd);
		return -1;
	}

	if(buf[0] == 0x30)
	{
		g_message("[parse_dsd2pcm_enable] off\n");
		close(fd);
		return 0;
	}
	else if (buf[0] == 0x31)
	{
		g_message("[parse_dsd2pcm_enable] on\n");
		close(fd);		
		return 1;
	}
	else
		g_message("[parse_dsd2pcm_enable] [%x][%c]\n", buf[0], buf[0]);

	close(fd);
	return -1;

}

int parse_dsd2pcm_freqhigh(void)
{
	char buf[4];
	int len;
	struct stat st={0};
	bool paused;

	int fd = open("/etc/blaster/dsd2pcm_freqhigh", O_RDONLY);

	if( fd < 0 )
	{
		g_message("[parse_dsd2pcm_freqhigh] file open fail\n");
		return -1;
	}

	fstat(fd, &st);
	
	len = read(fd, buf, 4);
	
	if(len<0) 
	{
		close(fd);
		g_message("[parse_dsd2pcm_freqhigh] file [%d] open fail1\n", fd);
		return -1;
	}

	if(buf[0] == 0x30)
	{
		g_message("[parse_dsd2pcm_freqhigh] 88.2KHz\n");
		close(fd);
		
		if(parse_dsd2pcm_enable()==1)
		{
			dsd_pcm_mode = 0;			  
		}
		return 0;
	}
	else if (buf[0] == 0x31)
	{
		g_message("[parse_dsd2pcm_freqhigh] 176.4KHz\n"); //ds_pcm -> 384(x), 88.2 176.4
		close(fd);
		
		if(parse_dsd2pcm_enable()==1)
		{
			dsd_pcm_mode = 1;
		}
		
		return 1;
	}
	else
		g_message("[parse_dsd2pcm_freqhigh] [%x][%c]\n", buf[0], buf[0]);


	close(fd);
	return -1;

}

static gpointer au_monitor_task(gpointer arg)
{
	int fd;
	int length, i;
	int wd;
	char buffer[BUF_LEN];

	fd = inotify_init();

	if ( fd < 0 ) 
	{
		perror( "inotify_init" );
	}

	wd = inotify_add_watch( fd, "/etc/blaster/", IN_CLOSE_WRITE );
//	wd = inotify_add_watch( fd, "/etc/blaster/dsd2pcm_freqhigh", IN_MOVE_SELF | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE );

	while (1)
	{
		length = read( fd, buffer, BUF_LEN );	

		if ( length < 0 ) 
		{
			perror( "read" );
		}	

		//g_message("read [%d]", length);
		i = 0;
		
		while ( i < length ) 
		{
			struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
			
			if ( event->len ) 
			{
				if(memcmp(event->name , "dsd2pcm_freqhigh", strlen(event->name))==0)
				{
					g_message( "[dsd2pcm_freqhigh] len=%d mask=%d name %s",  event->len, event->mask, event->name );
					pc_pause(global_player_control);					
					parse_dsd2pcm_freqhigh();					
				}
				if(memcmp(event->name , "dsd2pcm_enable", strlen(event->name))==0)
				{
					g_message( "[dsd2pcm_enable] len=%d mask=%d name %s",  event->len, event->mask, event->name );
					pc_pause(global_player_control);
					parse_dsd2pcm_freqhigh();
				}
				
			}

			i += EVENT_SIZE + event->len;
		}
	}

	return NULL;
}

void au_monitor(void)
{
	GError *e = NULL;
	int ret;

	ret = parse_dsd2pcm_freqhigh();
	
	g_message("[dsd_pcm_mode] = [%d]", dsd_pcm_mode);


	g_message("au_monitor_task start");	
	au_monitor_thr = g_thread_create(au_monitor_task, NULL, TRUE, &e);
}



