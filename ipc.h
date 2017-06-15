/*
 * A mordern IPC interface for sleepd/sleepctl (matzeton@googlemail.com)
 * (not Threadsafe!)
 */

#include <sys/types.h>
#include <grp.h>
#include <pthread.h>
#include <linux/limits.h>

#define IPC_MODE S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP

#define SET_FLAG(id_ptr, flg)   { id_ptr->flags |= flg; }
#define UNSET_FLAG(id_ptr, flg) { id_ptr->flags &= ~flg; }
#define GET_FLAG(id_ptr, flg)   (id_ptr->flags & flg)

#define FLG_RUNNING 0x1
#define FLG_ENABLED 0x2
#define FLG_HASX11  0x4
#define FLG_USEX11  0x8

#ifndef PATH_MAX
#define IPC_PATHMAX 256
#else
#define IPC_PATHMAX PATH_MAX
#endif
#define IPC_XDISPMAX 32

struct ipc_data
{
	pthread_mutex_t shm_mtx;
	unsigned char flags;
	int max_unused;
	int total_unused;

	char xauthority[PATH_MAX];
	char xdisplay[IPC_XDISPMAX];

	unsigned int xdiff_bounds[4];
	int xdiff_unused;

	int xmax_unused;
};

#ifdef IS_MASTER
extern int ipc_init_master (gid_t shm_gid);
extern void ipc_close_master (void);
#else
extern int ipc_init_slave (void);
#endif
extern void ipc_close_slave (void);

extern int ipc_lock (void);
extern int ipc_unlock (void);
extern int ipc_getshmptr(struct ipc_data **id);
