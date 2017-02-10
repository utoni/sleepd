// Description:
// polls /dev/input/event* for any update.
//
// Modified by Jeff Strunk
// Originally EventMonitor.c from keywatcher
// Copyright (C) 2002-2005 Frank Becker
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License,	or (at your option) any	later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details
//
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <linux/input.h>

#include "eventmonitor.h"

pthread_mutex_t activity_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t condition_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_cond = PTHREAD_COND_INITIALIZER;

void initializeIE(void) {
	int j=0;
	int i;
	int tmpfd;
	if (strncmp(eventData.events[0], "", 1) == 0) {
		int result;
		for (i=0; i<MAX_CHANNELS; i++) {
			char devName[128];
			snprintf(devName, 127, "/dev/input/event%d",i);
				result = access(devName, R_OK);
				if (result == 0) {
					strncpy(eventData.events[j], devName, 127);
					j++;
				}
			}
			strncpy(eventData.events[j], "", 1);
		}

		i=0;
		j=0;
		while (strncmp(eventData.events[i], "", 1) != 0) {
			tmpfd = open(eventData.events[i], O_RDONLY);
			if (tmpfd != -1) {
				eventData.channels[j] = tmpfd;
				j++;
			}
			i++;
		}
		eventData.channels[j] = -1;
}

void cleanupIE(void)  {
	int i;
	for (i=0; eventData.channels[i] != -1; i++) {
		close (eventData.channels[i]);
	}
}

void *eventMonitor() {
	int i, maxfd=0, retval;
	fd_set eventWatch;

	while (1) {
		initializeIE();
		FD_ZERO(&eventWatch);
		for (i=0; eventData.channels[i] != -1; i++) {
			FD_SET (eventData.channels[i], &eventWatch);
			if (eventData.channels[i] > maxfd)
				maxfd = eventData.channels[i];
		}

		maxfd++;

		pthread_mutex_lock(&condition_mutex);
		pthread_cond_wait(&condition_cond, &condition_mutex);
		pthread_mutex_unlock(&condition_mutex);

		pthread_mutex_lock(&activity_mutex);
		eventData.emactivity = 0;
		pthread_mutex_unlock(&activity_mutex);

		retval = select(maxfd, &eventWatch, NULL, NULL, NULL);

		if (retval > 0 ) {
			pthread_mutex_lock(&activity_mutex);
			eventData.emactivity = 1;
			pthread_mutex_unlock(&activity_mutex);
		}
		cleanupIE();
	}

	pthread_exit(NULL);
}
