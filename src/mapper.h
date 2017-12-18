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

#ifndef MPD_MAPPER_H
#define MPD_MAPPER_H

#include <stdbool.h>

#define PLAYLIST_FILE_SUFFIX ".m3u"
extern int debugFlag;
#ifdef SSD_CACHE
#include "player_control.h"
#define PIPE_PATH "/tmp/render_pipe"

#define FIFO_ADD 0
#define FIFO_UPDATE 1
#define FIFO_DELETED 2
#define FIFO_DELETE_FOREVER  3
#define PLAYING_FLAG 0x08
#endif


struct directory;
struct song;

void mapper_init(const char *_music_dir, const char *_playlist_dir);

void mapper_finish(void);

/**
 * Returns true if a music directory was configured.
 */
bool
mapper_has_music_directory(void);

/**
 * If the specified absolute path points inside the music directory,
 * this function converts it to a relative path.  If not, it returns
 * the unmodified string pointer.
 */
const char *
map_to_relative_path(const char *path_utf8);

/**
 * Determines the absolute file system path of a relative URI.  This
 * is basically done by converting the URI to the file system charset
 * and prepending the music directory.
 */
char *
map_uri_fs(const char *uri);

/**
 * Determines the file system path of a directory object.
 *
 * @param directory the directory object
 * @return the path in file system encoding, or NULL if mapping failed
 */
char *
map_directory_fs(const struct directory *directory);

/**
 * Determines the file system path of a directory's child (may be a
 * sub directory or a song).
 *
 * @param directory the parent directory object
 * @param name the child's name in UTF-8
 * @return the path in file system encoding, or NULL if mapping failed
 */
char *
map_directory_child_fs(const struct directory *directory, const char *name);

/**
 * Determines the file system path of a song.  This must not be a
 * remote song.
 *
 * @param song the song object
 * @return the path in file system encoding, or NULL if mapping failed
 */
char *
map_song_fs(const struct song *song);

/**
 * Maps a file system path (relative to the music directory or
 * absolute) to a relative path in UTF-8 encoding.
 *
 * @param path_fs a path in file system encoding
 * @return the relative path in UTF-8, or NULL if mapping failed
 */
char *
map_fs_to_utf8(const char *path_fs);

/**
 * Returns the playlist directory.
 */
const char *
map_spl_path(void);

/**
 * Maps a playlist name (without the ".m3u" suffix) to a file system
 * path.  The return value is allocated on the heap and must be freed
 * with g_free().
 *
 * @return the path in file system encoding, or NULL if mapping failed
 */
char *
map_spl_utf8_to_fs(const char *name);

#ifdef SSD_CACHE
#define SR_FORMPD	"/tmp/.blaster_sr"
#define AM_FORMPD	"/tmp/.blaster_am"
#define AM_FLAG         "/srv/widealab/audioMode"
#define AP_FLAG         "/srv/widealab/protectAMOLED"
#define SELF_AM_FLAG    "/tmp/.selfamon"
#define PAUSE_INTERVAL  "/srv/widealab/pause_interval"
size_t wl_buffer_read(int fd,int offset,void *ptr,size_t size);
bool fifo_ipc_init(void);
bool fifo_ipc_finish(void);
bool is_write_paused(void);
bool is_ssd_writing(void);
bool is_marked_reading(void);
bool mark_ssd_reading(void);
bool unmark_ssd_reading(void);
void write_render_pipe(char *cacheUri,int msg);
void mapper_get_cache_url(char *url,char* cache);
void mapper_get_cache_url_1(char *fileUrl,char *cacheUrl);
bool isCacheOK(char* fullCachePath);
void cacheNotExisted(char *cacheUrl);
void cacheExisted(char *cacheUrl);
bool isRegularAndReadable(char* path);
bool isStateFileRegularAndReadable(char *path);
void mapper_get_cache_hashuri(char* fullCachePath, char* cacheHashUri);
unsigned int getHash(char* str);
void deleteCache(char *c);
void addToCache(char *cacheUri);
void deleteCache(char *uri);
void send_sample_rate_cmd(int sample_rate);
void send_Y2_cmd(void);
void send_bit_width(int bw);
bool send_volume_cmd(int volume);
bool send_wakeup_cmd(void);
bool send_at0_cmd(void);
bool send_at1_cmd(void);
void send_stop_cmd(void);
bool send_blaster_stop_cmd(int s, int isDSD);
void clear_sample_rate(void);
bool send_sample_rate_cmd_and_pause(struct player_control *p,bool *b,int sample_rate, int isDSD);
void restartAirport(void);
void get_sample_rate_from_balster(void);
void update_opstamp(void);
void AMOLED_on_delay_off(void);
bool is_wakeup_cmd(char *cmd);
bool is_fading_enabled();
bool isCopying();
int  get_blaster_sr();
bool is_nas_lnk(const char *uri);
__off64_t  get_file_size_from_db(const char* path);
#endif

#endif
