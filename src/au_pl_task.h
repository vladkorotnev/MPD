int au_pl_task_active;
int au_pl_pos;

#define AU_MAX_PLIST 2000

unsigned enqueue_au_pl(const char *path, int uid, int to);

void au_pl_init(void);
void au_pl_stop(void);


