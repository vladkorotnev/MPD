#include "wimpmain.h"
typedef struct
{
        char user[WIMP_MIN_STRING*2];
        char passwd[WIMP_MIN_STRING];
		int type; // 0 for tidal 1 for wimp
        const char *token;
        char session[WIMP_MAX_STRING];
        char query_url[WIMP_MAX_STRING];
		char userID[WIMP_MIN_STRING];
		char countryCode[WIMP_MIN_STRING];
		char soundQuality[WIMP_MIN_STRING];
		char userQuality[WIMP_MIN_STRING];		
} conf_t;

struct au_stream_info
{
	int play_len;
	int total_len;	
};

#define WIMP_FORCELOGIN "/tmp/.wimp_forcelogin"
#define QOBUZ_FORCELOGIN "/tmp/.qobuz_forcelogin"
#define BUGS_FORCELOGIN "/tmp/.bugs_forcelogin"

char *url_encode(char *str) ;
char *removeBackSlash(char *str);
int u8_unescape(char *buf, int sz, char *src);
int au_parse_asx(const char *file, char *find_string, int dst_len);
int au_parse_ascii(const char *file, const char *search_tag, int skip_len, char *find_string, int dst_len);
int au_parse_string(const char *file, const char *search_tag, int skip_len, const char *tm, char *find_string, int dst_len);
int au_parse_string2(const char *file, const char *search_tag, const char *search_tag1, int skip_len, const char *tm, char *find_string, int dst_len);
int au_parse_string_qtitle(const char *file, const char *search_tag, int skip_len, const char *tm, char *find_string, int dst_len);

int au_parse_response(const char *file);
int au_parse_success(const char *file);

int au_parse_config(const char *file, conf_t *stream_conf );
int au_tag_save(const char *file, const char *tag);

int au_parse_uuid(const char *url, char *uuid);
int au_parse_uuid1(const char *url, char *uuid);
int au_parse_br(const char *url, char *breq);

int au_curl_option(CURL *handle);
int au_curl_set_mode(CURL *handle, FILE *outfile, char *query_url);

void set_curl_keepalive(CURL *c);

int wimp_get_url(const char *url);
int qobuz_get_url(const char *url);
int bugs_get_url(const char *url);
int shout_get_url(const char *url);

int wimp_save_song_tag(const char *url);
int qobuz_save_song_tag(const char *url);
int bugs_save_song_tag(const char *url);
int shout_save_song_tag(const char *url);


gpointer bugs_reportStreamingEnd(gpointer arg);
gpointer qobuz_reportStreamingStart(void);
gpointer qobuz_reportStreamingEnd(gpointer arg);
int au_stream_serviceStart(void);
int au_stream_serviceReady;

int au_ipaddr_check(void);


