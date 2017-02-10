/*
 * sleepd control program
 *
 * Copyright 2000, 2001 Joey Hess <joeyh@kitenet.net> under the terms of
 * the GNU GPL.
 */

#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "sleepd.h"

/* TODO: file locking */

pid_t getpid () {
	int f;
	char buf[10];
	pid_t pid;

	if ((f=open(PID_FILE, O_RDONLY)) == -1) {
		return -1;
	}
	if (read(f, buf, 9) == -1) {
		return -1;
	}
	pid=atoi(buf);
	if (pid < 1) {
		fprintf(stderr, "bad number in pid file");
		return -1;
	}
	return pid;
}

void usage () {
	fprintf(stderr, "Usage: sleepctl [on|off|status]\n");
}

int read_control () {
	int f;
	char buf[8];
	
	if ((f=open(CONTROL_FILE, O_RDONLY)) == -1) {
		return 0; // default
	}
	
	if (read(f, buf, 7) == -1) {
		perror("read");
		exit(2);
	}
	return atoi(buf);
}

int write_control (int value) {
	int f;
	char buf[10];
	pid_t pid;
	
	if ((f=open(CONTROL_FILE, O_WRONLY | O_CREAT, 0644)) == -1) {
		perror(CONTROL_FILE);
		exit(2);
	}
	snprintf(buf, 9, "%i\n", value);
	write(f, buf, strlen(buf));
	close(f);
	
	if ((pid = getpid()) == -1) {
		fprintf(stderr, "unable to determine daemon pid\n");
		exit(1);
	}
	if (kill(pid, SIGHUP) == -1) {
		perror("problem sending SIGHUP");
		exit(3);
	}
	
	return value;
}

void verify_daemon_running () {
	pid_t pid;
	if ((pid = getpid()) == -1 ||
	    (kill(getpid(), 0) == -1)) {
		printf("sleepd is not running\n");
	}
}

void show_status (int value) {
	switch (value) {
		case 0:
			printf("sleeping enabled\n");
			break;
		case 1:
			printf("sleeping disabled\n");
			break;
		case 2:
			printf("sleeping disabled (twice)\n");
			break;
		default:
			printf("sleeping disabled (%i times)\n", value);
	}
}

int main (int argc, char **argv) {
	int value;
	
	if (argc != 2) {
		usage();
		exit(1);
	}
	else if (strcmp(argv[1],"on") == 0) {
		value=read_control();
		if (value > 0)
			value--;
		show_status(write_control(value));
	}
	else if (strcmp(argv[1],"off") == 0) {
		show_status(write_control(read_control() + 1));
	}
	else if (strcmp(argv[1],"status") == 0) {
		verify_daemon_running();
		show_status(read_control());
	}
	else {
		usage();
		exit(1);
	}
	exit(0);
}
