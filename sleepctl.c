/*
 * sleepd control program
 *
 * Copyright 2000, 2001 Joey Hess <joeyh@kitenet.net>
 * Copyright 2017 Toni Uhlig <matzeton@googlemail.com>
 * under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "sleepd.h"
#include "ipc.h"

void cleanup_and_exit(int ret) __attribute__((noreturn));

void usage (void) {
	printf("sleepctl %d.%d\n", PKG_VERSION_MAJOR, PKG_VERSION_MINOR);
	fprintf(stderr, "Usage: sleepctl [on|off|xon|xoff|status|xdiff [XxY WxH]]\n");
}

void show_status (struct ipc_data *id) {
	if (id) {
		if (GET_FLAG(id, FLG_ENABLED) == 0) {
			printf("daemon.: disabled\n");
		}
		else {
			printf("daemon.: enabled\n");
		}

		if (GET_FLAG(id, FLG_HASX11) != 0) {
			if (GET_FLAG(id, FLG_USEX11) == 0) {
				printf("x11....: disabled\n");
			}
			else {
				printf("x11....: enabled\n");
				printf("xdiff..: [x = %u , y = %u , w = %u , h = %u]\n", id->xdiff_bounds[0], id->xdiff_bounds[1], id->xdiff_bounds[2], id->xdiff_bounds[3]);
				printf("xdiffu.: %d\n", id->xdiff_unused);
			}
			printf("xmax...: %d\n", id->xmax_unused);

			if (strnlen(id->xauthority, IPC_PATHMAX) > 0) {
				printf("XAUTH..: %.*s\n", IPC_PATHMAX, id->xauthority);
			}
			else {
				printf("XAUTH..: <not set>\n");
			}
			if (strnlen(id->xdisplay, IPC_XDISPMAX) > 0) {
				printf("XDISP..: %.*s\n", IPC_XDISPMAX, id->xdisplay);
			}
			else {
				printf("XDISP..: <not set>\n");
			}
		} else printf("x11....: <not implemented>\n");

		printf("unused.: %d\n", id->total_unused);
	}
}

void cleanup_and_exit(int ret) {
	ipc_unlock();
        ipc_close_slave();
	exit(ret);
}

int main (int argc, char **argv) {
	struct ipc_data *id = NULL;

	if (argc != 2 && argc != 4) {
		usage();
		exit(2);
	}

	int ret;
	if ( (ret = ipc_init_slave()) != 0) {
		switch (ret) {
			case -2:
				fprintf(stderr, "sleepctl: Wrong shared memory segment size. Maybe recompile sleepd/sleepctl?\n");
				exit(2);
			case -3:
				fprintf(stderr, "sleepctl: sleepd not initialized\n");
				exit(2);
		}
		switch (errno) {
			case ENAMETOOLONG:
			case ENOENT:
			case ENOTDIR:
			case EFAULT:
				fprintf(stderr, "sleepctl: sleepd started?\n");
				break;
			case EACCES:
				fprintf(stderr, "sleepctl: Permission denied. Maybe start sleepctl as root?\n");
				break;
			default:
				perror("ipc_init");
		}
		exit(1);
	}

	if (ipc_lock() == 0) {
		if (ipc_getshmptr(&id) != 0) {
			cleanup_and_exit(1);
		}
	} else {
		perror("ipc_lock");
		exit(1);
	}

	errno = 0;

	if (strcmp(argv[1],"on") == 0) {
		SET_FLAG(id, FLG_ENABLED);
		show_status(id);
	}
	else if (strcmp(argv[1],"off") == 0) {
		UNSET_FLAG(id, FLG_ENABLED);
		show_status(id);
	}
	else if (strcmp(argv[1],"xon") == 0) {
		if (GET_FLAG(id, FLG_HASX11) != 0) {
			if (getenv("DISPLAY") && getenv("XAUTHORITY")) {
				strncpy(&id->xdisplay[0], getenv("DISPLAY"), IPC_XDISPMAX);
				strncpy(&id->xauthority[0], getenv("XAUTHORITY"), IPC_PATHMAX);
				SET_FLAG(id, FLG_USEX11);
				show_status(id);
			}
			else printf("sleepctl: Environment variables DISPLAY or XAUTHORITY not set.\n");
		}
		else printf("sleepctl: <not implemented>\n");
	}
	else if (strcmp(argv[1],"xoff") == 0) {
		if (GET_FLAG(id, FLG_HASX11) != 0) {
			UNSET_FLAG(id, FLG_USEX11);
			memset(&id->xauthority[0], '\0', IPC_PATHMAX);
			memset(&id->xdisplay[0], '\0', IPC_XDISPMAX);
			show_status(id);
		}
		else printf("sleepctl: <not implemented>\n");
	}
	else if (strcmp(argv[1],"status") == 0) {
		show_status(id);
	}
	else if (strcmp(argv[1],"xdiff") == 0) {
		unsigned int x, y, w, h;
		if (sscanf(argv[2], "%ux%u", &x, &y) != 2 ||
			sscanf(argv[3], "%ux%u", &w, &h) != 2) {
			printf("sleepctl: Wrong format for `%s %s`. (example: xdiff 200x100 150x150)\n", argv[2], argv[3]);
		}
		else {
			id->xdiff_bounds[0] = x;
			id->xdiff_bounds[1] = y;
			id->xdiff_bounds[2] = w;
			id->xdiff_bounds[3] = h;
		}
	}
	else {
		usage();
	}

	if (errno != 0) {
		perror(__FUNCTION__);
		cleanup_and_exit(1);
	}
	cleanup_and_exit(0);
}
