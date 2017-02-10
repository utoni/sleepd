/*
 * sleep daemon
 *
 * Copyright 2000-2008 Joey Hess <joeyh@kitenet.net> under the terms of the
 * GNU GPL.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include "apm.h"
#include "acpi.h"
#ifdef HAL
#include "simplehal.h"
#endif
#ifdef UPOWER
#include "upower.h"
#endif
#include "eventmonitor.h"
#include "sleepd.h"

int irqs[MAX_IRQS]; /* irqs to examine have a value of 1 */
int autoprobe=1;
int have_irqs=0;
int use_events=1;
int max_unused=10 * 60; /* in seconds */
int ac_max_unused=0;
#ifdef USE_APM
char *apm_sleep_command="apm -s";
#endif
char *acpi_sleep_command="pm-suspend";
char *sleep_command=NULL;
char *hibernate_command=NULL;
int daemonize=1;
int sleep_time = DEFAULT_SLEEP_TIME;
int no_sleep=0;
signed int min_batt=-1;
#ifdef HAL
int use_simplehal = 0;
#endif
#ifdef UPOWER
int use_upower = 0;
#endif
int use_acpi=0;
int force_hal=0;
int require_unused_and_battery=0;	/* --and or -A option */
double max_loadavg = 0;
int use_utmp=0;
int use_net=0;
int min_tx=TXRATE;
int min_rx=RXRATE;
char netdevtx[MAX_NET][44];
char netdevrx[MAX_NET][44];
int debug=0;

void usage () {
	fprintf(stderr, "Usage: sleepd [-s command] [-d command] [-u n] [-U n] [-I] [-i n] [-E] [-e filename] [-a] [-l n] [-w] [-n] [-v] [-c n] [-b n] [-A] [-H] [-N [dev] [-t n] [-r n]]\n");
}

void parse_command_line (int argc, char **argv) {
	extern char *optarg;
	struct option long_options[] = {
		{"nodaemon", 0, NULL, 'n'},
		{"verbose", 0, NULL, 'v'},
		{"unused", 1, NULL, 'u'},
		{"ac-unused", 1, NULL, 'U'},
		{"load", 1, NULL, 'l'},
		{"utmp", 0, NULL, 'w'},
		{"no-irq", 0, NULL, 'I'},
		{"irq", 1, NULL, 'i'},
		{"no-events", 0, NULL, 'E'},
		{"event", 1, NULL, 'e'},
		{"help", 0, NULL, 'h'},
		{"sleep-command", 1, NULL, 's'},
		{"hibernate-command", 1, NULL, 'd'},
		{"auto", 0, NULL, 'a'},
		{"check-period", 1, NULL, 'c'},
		{"battery", 1, NULL, 'b'},
		{"and", 0, NULL, 'A'},
		{"netdev", 2, NULL, 'N'},
		{"rx-min", 1, NULL, 'r'},
		{"tx-min", 1, NULL, 't'},
		{"force-hal", 0, NULL, 'H'},
		{"force-upower", 0, NULL, 1},
		{0, 0, 0, 0}
	};
	int force_autoprobe=0;
	int noirq=0;
	int i;
	int c=0;
	int event=0;
	int netcount=0;
	int result;
	char tmpdev[8];
	char tx_statfile[44];
	char rx_statfile[44];

	while (c != -1) {
		c=getopt_long(argc,argv, "s:d:nvu:U:l:wIi:Ee:hac:b:AN::r:t:H", long_options, NULL);
		switch (c) {
			case 's':
				sleep_command=strdup(optarg);
				break;
			case 'd':
				hibernate_command=strdup(optarg);
				break;
			case 'n':
				daemonize=0;
				break;
			case 'v':
				debug=1;
				break;
			case 'u':
				max_unused=atoi(optarg);
				break;
			case 'U':
				ac_max_unused=atoi(optarg);
				break;
			case 'l':
				max_loadavg=atof(optarg);
				break;
			case 'w':
				use_utmp=1;
				break;
			case 1:
			case 'H':
				force_hal=1;
				break;
			case 'i':
				i = atoi(optarg);
				if ((i < 0) || (i >= MAX_IRQS)) {
					fprintf(stderr, "sleepd: bad irq number %d\n", i);
					exit(1);
				}
				irqs[atoi(optarg)]=1;
				autoprobe=0;
				have_irqs=1;
				break;
			case 'e':
				result = access(optarg, R_OK);
				switch(result) {
					case 0:
						strncpy(eventData.events[event], optarg,127);
						use_events=1;
						event++;
						break;
					case ELOOP:
					case ENAMETOOLONG:
					case ENOENT:
					case ENOTDIR:
					case EFAULT:
						fprintf(stderr, "sleepd: event file not found: %s\n", optarg);
						exit(1);
					case EACCES:
						fprintf(stderr, "sleepd: can't read %s\n", optarg);
						exit(1);
				}
				break;
			case 'E':
				use_events=0;
				break;
			case 'a':
				force_autoprobe=1;
				break;
			case 'I':
				noirq=1;
				break;
			case 'h':
				usage();
				exit(0);
				break;
			case 'c':
				sleep_time=atoi(optarg);
				if (sleep_time <= 0) {
					fprintf(stderr, "sleepd: bad sleep time %d\n", sleep_time);
					exit(1);
				}
				break;
			case 'b':
				min_batt=atoi(optarg);
				if (min_batt < 0) {
					fprintf(stderr, "sleepd: bad minimumn battery percentage %d\n", min_batt);
					exit(1);
				}
				break;
			case 'N':
				if (optarg == NULL) {
					if (netcount == 0) {
						sprintf(tmpdev, "eth0");
					}
					else {
						fprintf(stderr, "sleepd: multiple -N options with no arguments\n");
						exit(1);
					}
				}
				else {
					strncpy(tmpdev, optarg, 8);
				}
				sprintf(tx_statfile, TXFILE, tmpdev);
				sprintf(rx_statfile, RXFILE, tmpdev);
				if ((access(tx_statfile, R_OK) == 0) &&
				    (access(rx_statfile, R_OK) == 0)) {
					strncpy(netdevtx[netcount], tx_statfile, 44);
					strncpy(netdevrx[netcount], rx_statfile, 44);
					use_net=1;
					netcount++;
				}
				else {
					fprintf(stderr, "sleepd: %s not found in sysfs\n", tmpdev);
					exit(1);
				}
				break;
			case 't':
				min_tx = atoi(optarg);
				break;
			case 'r':
				min_rx = atoi(optarg);
				break;
			case 'A':
				require_unused_and_battery=1;
				break;
		}
	}
	if (optind < argc) {
		usage();
		exit(1);
	}

	if (use_events)
		strncpy(eventData.events[event], "", 1);

	if (use_net)
		strncpy(netdevtx[netcount], "", 1);
		strncpy(netdevrx[netcount], "", 1);

	if (noirq)
		autoprobe=0;

	if (force_autoprobe)
		autoprobe=1;
}

void loadcontrol (int signum) {
	int f;
	char buf[8];
	
	if (((f=open(CONTROL_FILE, O_RDONLY)) == -1) ||
	     (flock(f, LOCK_SH) == -1) ||
	     (read(f, buf, 7) == -1))
		return;
	no_sleep=atoi(buf);
	close(f);

	signal(SIGHUP, loadcontrol);
}

void writecontrol (int value) {
	int f;
	char buf[10];

	if ((f=open(CONTROL_FILE, O_WRONLY | O_CREAT, 0644)) == -1) {
		perror(CONTROL_FILE);
	}
	snprintf(buf, 9, "%i\n", value);
	write(f, buf, strlen(buf));
	close(f);
}

/**** stat the device file to get an idle time */
// Copied from w.c in procps by Charles Blake
int idletime (const char *tty) {
	struct stat sbuf;
	if (stat(tty, &sbuf) != 0)
		return 0;
	return (int)(time(NULL) - sbuf.st_atime);
}

int check_irqs (int activity, int autoprobe) {
	static long irq_count[MAX_IRQS]; /* holds previous counters of the irqs */
	static int probed=0;
	static int no_dev_warned=0;

	FILE *f;
	char line[64];
	int i;

	f=fopen(INTERRUPTS, "r");
	if (! f) {
		perror(INTERRUPTS);
		exit(1);
	}
	while (fgets(line,sizeof(line),f)) {
		long v;
		int do_this_one=0;
		if (autoprobe) {
			/* Lowercase line. */
			for(i=0;line[i];i++)
				line[i]=tolower(line[i]);
			/* See if it is a keyboard or mouse. */
			if (strstr(line, "mouse") != NULL ||
			    strstr(line, "keyboard") != NULL ||
			    /* 2.5 kernels report by chipset,
			     * this is a ps/2 keyboard/mouse. */
			    strstr(line, "i8042") != NULL) {
				do_this_one=1;
				probed=1;
			}
		}
		if (sscanf(line,"%d: %ld",&i, &v) == 2 &&
		    i < MAX_IRQS &&
		    (do_this_one || irqs[i]) && irq_count[i] != v) {
			if (debug)
				printf("sleepd: activity: irq %d\n", i);
			activity=1;
			irq_count[i] = v;
		}
	}
	fclose(f);
	
	if (autoprobe && ! probed) {
		if (! no_dev_warned) {
			no_dev_warned=1;
			syslog(LOG_WARNING, "no keyboard or mouse irqs autoprobed");
		}
	}

	return activity;
}

int check_net (int activity) {
	static long tx_count[MAX_NET]; /* holds previous counters of tx packets */
	static long rx_count[MAX_NET]; /* holds previous counters of rx packets */

	long tx,rx;
	int i;
	for (i=0; i < MAX_NET; i++) {
		if (strncmp(netdevtx[i], "", 1) != 0) {
			char line[64];
			FILE *f=fopen(netdevtx[i], "r");
			if (fgets(line,sizeof(line),f)) {
				tx = strtol(line, (char **) NULL, 10);
			}
			else {
				fprintf(stderr, "sleepd: could not read %s\n", netdevtx[i]);
				exit(1);
			}
			fclose(f);
			f=fopen(netdevrx[i], "r");
			if (fgets(line,sizeof(line),f)) {
				rx = strtol(line, (char **) NULL, 10);
			}
			else {
				fprintf(stderr, "sleepd: could not read %s\n", netdevrx[i]);
				exit(1);
			}
			fclose(f);
			if (((tx - tx_count[i])/sleep_time > min_tx) ||
			    ((rx - rx_count[i])/sleep_time > min_rx)) {
				if (debug) {
					printf("sleepd: activity: network txrate: %ld rxrate: %ld\n",
						(tx - tx_count[i])/sleep_time, (rx - rx_count[i])/sleep_time);
				}
				activity=1;
			}
			tx_count[i]=tx;
			rx_count[i]=rx;
		}
		else {
			break;
		}
	}

	return activity;
}

int check_utmp (int total_unused) {
	/* replace total_unused with the minimum of
	 * total_unused and the shortest utmp idle time. */
	typedef struct utmp utmp_t;
	utmp_t *u;
	int min_idle=2*max_unused;
	utmpname(UTMP_FILE);
	setutent();
	while ((u=getutent())) {
		if (u->ut_type == USER_PROCESS) {
			/* get tty. From w.c in procps by Charles Blake. */
			char tty[5 + sizeof u->ut_line + 1] = "/dev/";
			int i;
			for (i=0; i < sizeof u->ut_line; i++) {
				/* clean up tty if garbled */
				if (isalnum(u->ut_line[i]) ||
				    (u->ut_line[i]=='/')) {
					tty[i+5] = u->ut_line[i];
				}
				else {
					tty[i+5] = '\0';
				}
			}
			int cur_idle=idletime(tty);
			min_idle = (cur_idle < min_idle) ? cur_idle : min_idle;
		}
	}
	/* The shortest idle time is the real idle time */
	total_unused = (min_idle < total_unused) ? min_idle : total_unused;
	if (debug && total_unused == min_idle)
		printf("sleepd: activity: utmp %d seconds\n", min_idle);

	return total_unused;
}

void main_loop (void) {
	int activity=0, sleep_now=0, total_unused=0;
	int sleep_battery=0;
	int prev_ac_line_status=-1;
	time_t nowtime, oldtime=0;
	apm_info ai;
	double loadavg[1];

	if (use_events) {
		pthread_t emthread;
		pthread_create(&emthread, NULL, eventMonitor, NULL);
	}

	while (1) {
		activity=0;
		if (use_events) {
			pthread_mutex_lock(&condition_mutex);
			pthread_cond_signal(&condition_cond);
			pthread_mutex_unlock(&condition_mutex);
		}

		if (use_acpi) {
			acpi_read(1, &ai);
		}
#ifdef HAL
		else if (use_simplehal) {
			simplehal_read(1, &ai);
		}
#endif
#ifdef UPOWER
		else if (use_upower) {
			upower_read(1, &ai);
		}
#endif
#if defined(USE_APM)
		else {
			apm_read(&ai);
		}
#else
		else {
			syslog(LOG_CRIT, "APM support is disabled, no other methods available. Abort.");
			abort();
		}
#endif

		if (debug)
			printf("sleepd: battery level: %d%%, remaining time: %c%d:%02d\n",
				ai.battery_percentage,
				(ai.battery_time < 0) ? '-' : ' ',
				abs(ai.battery_time) / 3600, (abs(ai.battery_time) / 60) % 60);

		if (min_batt != -1 && ai.ac_line_status != 1 && 
		    ai.battery_percentage != -1 &&
		    ai.battery_percentage < min_batt &&
		    ai.battery_status != BATTERY_STATUS_ABSENT) {
			sleep_battery = 1;
		}

		if (sleep_battery && ! require_unused_and_battery) {
			syslog(LOG_NOTICE, "battery level %d%% is below %d%%; forcing hibernation", ai.battery_percentage, min_batt);
			if (system(hibernate_command) != 0)
				syslog(LOG_ERR, "%s failed", hibernate_command);
			/* This counts as activity; to prevent double sleeps. */
			if (debug)
				printf("sleepd: activity: just woke up\n");
			activity=1;
			oldtime=0;
			sleep_battery=0;
		}

		if ((ai.ac_line_status != prev_ac_line_status) && (prev_ac_line_status != -1)) {
			/* AC plug/unplug counts as activity. */
			if (debug)
				printf("sleepd: activity: AC status change\n");
			activity=1;
		}
		prev_ac_line_status=ai.ac_line_status;

		/* Rest is only needed if sleeping on inactivity. */
		if (! max_unused && ! ac_max_unused) {
			sleep(sleep_time);
			continue;
		}

		if (autoprobe || have_irqs) {
			activity=check_irqs(activity, autoprobe);
		}

		if (use_net) {
			activity=check_net(activity);
		}

		if ((max_loadavg != 0) &&
		    (getloadavg(loadavg, 1) == 1) &&
		    (loadavg[0] >= max_loadavg)) {
			/* If the load average is too high */
			if (debug)
				printf("sleepd: activity: load average %f\n", loadavg[0]);
			activity=1;
		}

		if (use_utmp == 1) {
			total_unused=check_utmp(total_unused);
		}

		sleep(sleep_time);

		if (use_events) {
			pthread_mutex_lock(&activity_mutex);
			if (eventData.emactivity == 1) {
				if (debug)
					printf("sleepd: activity: keyboard/mouse events\n");
				activity=1;
			}
			pthread_mutex_unlock(&activity_mutex);
		}

		if (activity) {
			total_unused = 0;
		}
		else {
			total_unused += sleep_time;
			if (ai.ac_line_status == 1) {
				/* On wall power. */
				if (ac_max_unused > 0) {
					sleep_now = total_unused >= ac_max_unused;
				}
			}
			else if (max_unused > 0) {
				sleep_now = total_unused >= max_unused;
			}

			if (sleep_now && ! no_sleep && ! require_unused_and_battery) {
				syslog(LOG_NOTICE, "system inactive for %ds; forcing sleep", total_unused);
				if (system(sleep_command) != 0)
					syslog(LOG_ERR, "%s failed", sleep_command);
				total_unused=0;
				oldtime=0;
				sleep_now=0;
			}
			else if (sleep_now && ! no_sleep && sleep_battery) {
				syslog(LOG_NOTICE, "system inactive for %ds and battery level %d%% is below %d%%; forcing hibernaton", 
				       total_unused, ai.battery_percentage, min_batt);
				if (system(hibernate_command) != 0)
					syslog(LOG_ERR, "%s failed", hibernate_command);
				total_unused=0;
				oldtime=0;
				sleep_now=0;
				sleep_battery=0;
			}
		}
		
		/*
		 * Keep track of how long it's been since we were last
		 * here. If it was much longer than sleep_time, the system
		 * was probably suspended, or this program was, (or the 
		 * kernel is thrashing :-), so clear idle counter.
		 */
		nowtime=time(NULL);
		/* The 1 is a necessary fudge factor. */
		if (oldtime && nowtime - sleep_time > oldtime + 1) {
			no_sleep=0; /* reset, since they must have put it to sleep */
			writecontrol(no_sleep);
			syslog(LOG_NOTICE,
					"%i sec sleep; resetting timer",
					(int)(nowtime - oldtime));
			total_unused=0;
		}
		oldtime=nowtime;
	}
}

void cleanup (int signum) {
	if (daemonize)
		unlink(PID_FILE);
	exit(0);
}

int main (int argc, char **argv) {
	FILE *f;

	parse_command_line(argc, argv);
	
	/* Log to the console if not daemonizing. */
	openlog("sleepd", LOG_PID | (daemonize ? 0 : LOG_PERROR), LOG_DAEMON);
	
	/* Set up a signal handler for SIGTERM to clean up things. */
	signal(SIGTERM, cleanup);
	/* And a handler for SIGHUP, to reaload control file. */
	signal(SIGHUP, loadcontrol);
	loadcontrol(0);

	if (! use_events) {
		if (! have_irqs && ! autoprobe) {
			fprintf(stderr, "No irqs specified.\n");
			exit(1);
		}
	}

	if (daemonize) {
		if (daemon(0,0) == -1) {
			perror("daemon");
			exit(1);
		}
		if ((f=fopen(PID_FILE, "w")) == NULL) {
			syslog(LOG_ERR, "unable to write %s", PID_FILE);
			exit(1);
		}
		else {
			fprintf(f, "%i\n", getpid());
			fclose(f);
		}
	}
	
	if (force_hal
#ifdef USE_APM
		|| apm_exists() != 0
#else
		|| 1
#endif
	    ) {
		if (! sleep_command)
			sleep_command=acpi_sleep_command;

		/* Chosing between hal and acpi backends is tricky,
		 * because acpi may report no batteries due to the battery
		 * being absent (but inserted later), or due to battery
		 * info no longer being available by acpi in new kernels.
		 * Meanwhile, hal may fail if hald is not yet
		 * running, but might work later.
		 *
		 * The strategy used is to check if acpi reports an AC
		 * adapter, or a battery. If it reports neither, we assume
		 * that the kernel no longer supports acpi power info, and
		 * use hal.
		 */
		if (! force_hal && acpi_supported() &&
		    (acpi_ac_count > 0 || acpi_batt_count > 0)) {
			use_acpi=1;
		}
#if defined(HAL)
		else if (simplehal_supported()) {
			use_simplehal=1;
		}
		else {
			syslog(LOG_NOTICE, "failed to connect to hal on startup, but will try to use it anyway");
			use_simplehal=1;
		}
#elif defined(UPOWER)
		else if (upower_supported()) {
			use_upower = 1;
		}
		else {
			syslog(LOG_NOTICE, "failed to connect to upower");
		}
#else
		else {
			fprintf(stderr, "sleepd: no APM or ACPI support detected\n");
			exit(1);
		}
#endif
	}
	if (! sleep_command) {
#if defined(USE_APM)
		sleep_command=apm_sleep_command;
#else
		sleep_command=acpi_sleep_command;
#endif
	}
	if (! hibernate_command) {
		hibernate_command=sleep_command;
	}
	
	main_loop();
	
	return(0); // never reached
}
