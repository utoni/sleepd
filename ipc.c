/*
 * A mordern IPC interface for sleepd/sleepctl (matzeton@googlemail.com)
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>    /* memcpy */
#include <fcntl.h>     /* For O_* constants */
#include <sys/stat.h>  /* For mode constants */
#include <semaphore.h> /* POSIX Semaphores */
#include <sys/mman.h>  /* POSIX Shared Memory + mmap */
#include <errno.h>     /* errno */
#include "ipc.h"
#include "sleepd.h"

struct ipc_data *ip = NULL;

#ifdef IS_MASTER
int ipc_init_master (gid_t shm_grp) {
	int shm_fd = -1;

	/* open shared memory only once */
	if (!ip) {
		errno = 0;
		/* disable umask temporarily */
		mode_t old_umask = umask(0);
		shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, IPC_MODE);
		umask(old_umask);
		if (shm_fd >= 0) {
			/* set shared memory access rights, see /dev/shm */
			uid_t myuid = getuid();
			if (fchown(shm_fd, myuid, shm_grp) != 0)
				perror("fchown");
		}
	}
	if (shm_fd >= 0) {
		/* map memory segment in our address space */
		size_t siz = sizeof(struct ipc_data);
		if (ftruncate(shm_fd, siz) != 0)
			return -1;
		if ((ip = (struct ipc_data *)mmap(NULL, siz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
			return -1;
		close(shm_fd);

		/* init process shared mutex */
		pthread_mutexattr_t mutex_attr;
		pthread_mutexattr_init(&mutex_attr);
		pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
		if (pthread_mutex_init(&ip->shm_mtx, &mutex_attr) != 0)
			return -1;
		/* lock mutex and set FLG_RUNNING to avoid a possible race condition */
		pthread_mutex_lock(&ip->shm_mtx);
		SET_FLAG(ip, FLG_RUNNING);
		SET_FLAG(ip, FLG_ENABLED);
		UNSET_FLAG(ip, FLG_USEX11);
#ifdef X11
		SET_FLAG(ip, FLG_HASX11);
#else
		UNSET_FLAG(ip, FLG_HASX11);
#endif
		pthread_mutex_unlock(&ip->shm_mtx);
	} else return -1;
	return 0;
}

void ipc_close_master (void) {
	if (ip) {
		ipc_lock();
		UNSET_FLAG(ip, FLG_RUNNING);
		ipc_unlock();
	}
	ipc_close_slave();
	shm_unlink(SHM_NAME);
}
#endif

int ipc_init_slave (void) {
	int shm_fd = -1;

	if (!ip) {
		errno = 0;
		shm_fd = shm_open(SHM_NAME, O_RDWR, IPC_MODE);
	}
	if (shm_fd >= 0) { 
		size_t siz = sizeof(struct ipc_data);
		if ((ip = (struct ipc_data *)mmap(NULL, siz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
			return -1;
		close(shm_fd);

		unsigned i;
		for (i = 0; i < IPC_MAXTRIES; ++i) {
			if (GET_FLAG(ip, FLG_RUNNING) == 0)
				sched_yield();
		}
		/* avoid a possible race condition (see ipc_init_master above) */
		if (GET_FLAG(ip, FLG_RUNNING) == 0) {
			return -1;
		}
		pthread_mutex_lock(&ip->shm_mtx);
		pthread_mutex_unlock(&ip->shm_mtx);
        } else return -1;
        return 0;
}

void ipc_close_slave (void) {
	if (munmap(ip, sizeof(struct ipc_data)) == 0)
		ip = NULL;
}

int ipc_lock (void) {
	if (!ip)
		return -1;
	unsigned tries = IPC_MAXTRIES;
	errno = 0;
	do {
		if (errno != 0) {
			sched_yield();
			errno = 0;
		}
		pthread_mutex_trylock(&ip->shm_mtx);
	} while (errno == EAGAIN && tries--);
	return errno;
}

int ipc_unlock (void) {
	if (!ip)
		return -1;
	errno = 0;
	pthread_mutex_unlock(&ip->shm_mtx);
	return errno;
}

int ipc_getshmptr (struct ipc_data **id) {
	if (id && ip) {
		*id = ip;
		return 0;
	} else return -1;
}
