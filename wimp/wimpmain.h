#ifdef WIMP_SUPPORT
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define WIMP_TAG_TITLE "/tmp/.wimp_mpd_title"
#define WIMP_TAG_ARTIST "/tmp/.wimp_mpd_artist"
#define WIMP_TAG_QUALITY "/tmp/.wimp_mpd_quality"
#define WIMP_TAG_DEFAULT_TITLE "UNKNOWN TITLE"
#define WIMP_TAG_DEFAULT_ARTIST "TIDAL STREAM"

#define WIMP_MAX_RETRY 				2
#define WIMP_MIN_STRING             32
#define WIMP_MAX_STRING             512

int is_tidal(const char *url);
int is_plist(const char *url);

char au_stream_url[WIMP_MAX_STRING];
char au_stream_log_url[WIMP_MAX_STRING];

//GMutex *au_mutex;
//GCond *au_cond;

CURL *bugs_curl_handle;
CURL *qobuz_curl_handle;
CURL *wimp_curl_handle;
CURL *shout_curl_handle;

int wimp_init(void);
int qobuz_init(void);
int bugs_init(void);
int shout_init(void);

int au_stream_get_url(const char *url);
int au_stream_get_tag(const char *url);
int au_stream_check(const char *url);

int au_send_stream_start(void);
int au_send_stream_info(void *info, int stream_type);
int au_send_stream_init(void);

#endif

