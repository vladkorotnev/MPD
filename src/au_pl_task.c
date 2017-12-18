#include <glib.h>
#include <assert.h>
#include <sqlite3.h>

#include "command.h"
#include "playlist.h"
#include "player_control.h"
#include "playlist.h"
#include "playlist_error.h"
#include "main.h"
#include "queue.h"

#include "au_pl_task.h"
#include "mapper.h"


#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "au_pl_task"

static struct {
	char *path;
	bool discard;
	int to;
} au_plist[AU_MAX_PLIST];

GMutex *mutex;
GCond *cond;
GCond *stop_cond;

int au_pl_uid = 0;
int au_pl_stop_progress;
int au_pl_start_nextadd;
int au_pl_remove_count;

static GThread *au_pl_thr = NULL;
struct player_control *au_pl_pc = NULL;

static void au_pl_clear(void);


static void set_event(void)  
{  
    au_pl_task_active = 1;  
	//g_message("set_event [%d]\n", au_pl_task_active);
    g_cond_signal(cond);  	
}  

static int wait_event(int wait_time)  
{  
    int ret = -1;  
   
    GTimeVal t;  
    t.tv_sec = time(NULL) + wait_time;  
    t.tv_usec = 0;  

    if ( g_cond_timed_wait(cond,mutex, &t) )  
    {  
        ret = 1;  
    }  
  
    return ret;  
}  

static gpointer au_pl_task(gpointer arg)
{

	int i; 
	int ret = 0;
	int result = 0;
	
	//g_message("au_pl_task ret = [%d] [%d]\n", ret, au_pl_task_active );
	ret = wait_event(5);
	//g_message("au_pl_task1 ret = [%d] [%d]\n", ret, au_pl_task_active );

	if( ret ==1)
	{
		for(i=0;i<=au_pl_pos; i++)
		{
			//g_message("===> [%d][%d] \n",i, au_pl_stop_progress);

			if(au_pl_stop_progress == 1)
			{
				g_cond_signal(stop_cond);
				break;
			}

			if(au_plist[i].discard == false)
			{
				unsigned added_id;

				result = playlist_append_file(&g_playlist, global_player_control, au_plist[i].path, au_pl_uid, &added_id);
			
				if(au_plist[i].to != -1)
				{
						if(result==PLAYLIST_RESULT_SUCCESS)
						{
							result = playlist_move_id(&g_playlist, global_player_control, added_id, au_plist[i].to - au_pl_remove_count);
						}
						else // playlist_append_file fail
						{
							g_message("===> skip bad song [%d] [%d] count=[%d]", added_id, au_plist[i].to, au_pl_remove_count);
							au_pl_remove_count++;
						}
				}
				
				g_message("[%d] [%d]->[%d][%s][%d]\n", i, added_id, au_plist[i].to - au_pl_remove_count, au_plist[i].path, result);
				au_plist[i].discard = true;
				g_usleep (G_USEC_PER_SEC/10);
				g_free(au_plist[i].path);
			}
			
			if((i%10) == 0)
			{
				g_usleep (G_USEC_PER_SEC/2);
			}
		}
		au_pl_clear();
	}

	au_pl_task_active = -1;
}

static void au_pl_clear(void)
{
	au_pl_task_active = 0;	
	au_pl_stop_progress = 0;
	au_pl_pos = 0;
	au_pl_start_nextadd = 0;
	au_pl_remove_count = 0;
}

static void au_plist_init(void)
{
	int i;
	
	for(i=0;i<AU_MAX_PLIST; i++)
	{
		au_plist[i].to = -1;
		au_plist[i].discard = true;
	}
	
	au_pl_clear();
}

void au_pl_init(void)
{
	GError *e = NULL;
	au_plist_init();

	mutex = g_mutex_new();
	cond = g_cond_new();
	stop_cond = g_cond_new();
	au_pl_task_active = 0;
	
	au_pl_thr = g_thread_create(au_pl_task, NULL, true, &e);
	g_thread_set_priority (au_pl_thr, G_THREAD_PRIORITY_LOW);
}


void au_pl_stop(void)
{
	if(au_pl_task_active ==1)
	{
		bool ret = false;
		GTimeVal t;  
		
		au_pl_stop_progress = 1;
		
		t.tv_sec = time(NULL) + 5;	
		t.tv_usec = 0;	
		
		ret = g_cond_timed_wait(stop_cond,mutex, &t);

		g_message("au_pl_stop wait ret [%s]\n", ret==true ? "OK" : "TIMEOUT");
	}

	//g_message("au_pl_stop pos=[%d] stop=[%d]\n", au_pl_pos, au_pl_stop_progress);
}

int au_pl_add_count;
enum playlist_result enqueue_au_pl(const char *path, int uid, int to)
{	
	if(au_pl_pos >= AU_MAX_PLIST)
	{
		g_message("==> enqueue_au_pl wait until job end[%d]\n", au_pl_pos);
		return PLAYLIST_RESULT_DENIED;
	}
	
	if(au_pl_task_active == -1)
	{
		GError *e = NULL;

		if(au_pl_thr)
		{
			g_thread_join(au_pl_thr);
			//g_message("===> enqueue_au_pl thread joined [%d]\n", au_pl_task_active);
		}
		
		au_pl_thr = g_thread_create(au_pl_task, NULL, true, &e);
		g_thread_set_priority (au_pl_thr, G_THREAD_PRIORITY_LOW);
		au_pl_task_active = 0;		
		g_usleep (G_USEC_PER_SEC/10);
		//g_message("===> enqueue_au_pl thread restart [%d]\n", au_pl_task_active);
	}
	
	if(au_pl_uid == 0)
		au_pl_uid = uid;
	
	au_plist[au_pl_pos].path = g_strdup(path);
	//strcpy(au_plist[au_pl_pos].path, path);
	au_plist[au_pl_pos].discard = false;
		
	if(to != -1)
	{
		if(au_pl_start_nextadd == 0)
		{
			au_pl_start_nextadd = to;
			au_pl_add_count = 0; // au_pl_task start with index=1, index=0 use original handle_add metthod
		}

		au_plist[au_pl_pos].to = au_pl_start_nextadd + au_pl_add_count++; //Use local index

	}
	else
		au_plist[au_pl_pos].to = to;

	//g_message("enqueue_au_pl pos=[%d] to [%d] au_pl_start_nextadd[%d] final[%d]\n", au_pl_pos, to, au_pl_start_nextadd, au_plist[au_pl_pos].to );

	if(au_pl_task_active == 0)
		set_event();	
	
	au_pl_pos++;

	return PLAYLIST_RESULT_SUCCESS;
}


