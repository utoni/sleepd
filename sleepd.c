/*
 * sleep daemon
 *
 * Copyright 2000-2008 Joey Hess <joeyh@kitenet.net>
 * Copyright 2017 Toni Uhlig <matzeton@googlemail.com>
 * under the terms of the GNU GPL.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

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
#include <grp.h>

#include "apm.h"
#include "acpi.h"
#ifdef HAL
#include "simplehal.h"
#endif
#ifdef UPOWER
#include "upower.h"
#endif
#ifdef X11
#include <X11/Xutil.h>
#include <pwd.h>
#include "xutils.h"
#endif
#include "eventmonitor.h"
#include "sleepd.h"
#include "ipc.h"


#define ARRAY_SIZE(array) (sizeof((array))/sizeof((array)[0]))
#define ENV_LEN 1024


static int irqs[MAX_IRQS];		/* irqs to examine have a value of 1 */
static unsigned char autoprobe = 1;
static unsigned char have_irqs = 0;
static unsigned char use_events = 1;
static pthread_t emthread;
static int max_unused = MAX_UNUSED;	/* in seconds */
static int ac_max_unused = 0;
#ifdef USE_APM
static char *apm_sleep_command = "apm -s";
#endif
static char *acpi_sleep_command = "pm-suspend";
static char *sleep_command = NULL;
static char *hibernate_command = NULL;
static unsigned char daemonize = 1;
static int sleep_time = DEFAULT_SLEEP_TIME;
static unsigned char no_sleep=0;
static int min_batt=-1;
#ifdef HAL
static unsigned char use_simplehal = 0;
#endif
#ifdef UPOWER
static unsigned char use_upower = 0;
#endif
static unsigned char use_acpi = 0;
static unsigned char force_hal = 0;
static unsigned char require_unused_and_battery = 0;	/* --and or -A option */
static double max_loadavg = 0;
static unsigned char use_utmp = 0;
static unsigned char use_net = 0;
static int min_tx[MAX_NET]; 
static int min_rx[MAX_NET];
static unsigned char net_samples[MAX_NET];		/* net samples for rx/tx per netdev */
static size_t net_samples_idx = 0;			/* current index of net_samples */
static unsigned int net_samples_rx[MAX_NET][MAX_SAMPLES]; /* save rx samples[net_samples_idx] */
static unsigned int net_samples_tx[MAX_NET][MAX_SAMPLES]; /* save tx samples[net_samples_idx] */
static char netdevtx[MAX_NET][45];
static char netdevrx[MAX_NET][45];
#ifdef X11
static unsigned char use_x = 0;
static unsigned int use_xdiff = 0;
static int xmax_unused = 0;
static int xdiff_max_unused = 0;
#endif
static gid_t shm_grp = 0;
static unsigned char debug = 0;


void usage (char *arg0) {
	fprintf(stderr, "Usage: sleepd [-s command] [-d command] [-u n] [-U n] [-I] [-i n] [-E] [-e filename] [-a] [-l n] [-w] [-n] [-v] [-c n] [-b n] [-A] [-H] [-N [dev] [-t n] [-r n] -s n] [-x n] [-X] [-g name] [--xdiff-unused n] [-V] [-h]\n\n");
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
                {"net-samples", 1, NULL, 'm'},
		{"xunused", 1, NULL, 'x'},
		{"xdiff", 1, NULL, 'X'},
		{"xdiff-unused", 1, NULL, 2},
		{"group", 1, NULL, 'g'},
		{"force-hal", 0, NULL, 'H'},
		{"force-upower", 0, NULL, 1},
		{"version", 0, NULL, 'V'},
		{"help", 0, NULL, 'h'},
		{0, 0, 0, 0}
	};
	unsigned char force_autoprobe = 0;
	unsigned char noirq = 0;
	int i;
	int c = 0;
	int event = 0;
	int netcount = 0;
	int result;
	char tmpdev[9];
	char tx_statfile[44];
	char rx_statfile[44];
	unsigned char tx_set[MAX_NET];
	unsigned char rx_set[MAX_NET];
	unsigned char sm_set[MAX_NET];

	memset(&tx_set[0], '\0', MAX_NET*sizeof(char));
	memset(&rx_set[0], '\0', MAX_NET*sizeof(char));
	memset(&sm_set[0], '\0', MAX_NET*sizeof(char));

	while (c != -1) {
		c = getopt_long(argc,argv, "s:d:nvu:U:l:wIi:Ee:Vhac:b:AN:r:t:x:X:m:g:H", long_options, NULL);
		switch (c) {
			case 's':
				if (sleep_command) {
					fprintf(stderr, "sleepd: multiple -s found\n");
					exit(1);
				}
				sleep_command = strdup(optarg);
				break;
			case 'd':
				if (hibernate_command) {
					fprintf(stderr, "sleepd: multiple -d found\n");
					exit(1);
				}
				hibernate_command = strdup(optarg);
				break;
			case 'n':
				daemonize = 0;
				break;
			case 'v':
				debug = 1;
				break;
			case 'u':
				max_unused = atoi(optarg);
                                if (max_unused < MINIMAL_UNUSED) {
					fprintf(stderr, "sleepd: bad max unused time %d < %d\n", max_unused, MINIMAL_UNUSED);
					exit(1);
				}
				break;
			case 'U':
				ac_max_unused = atoi(optarg);
				if (ac_max_unused < MINIMAL_UNUSED) {
					fprintf(stderr, "sleepd: bad ac max unused time %d < %d\n", ac_max_unused, MINIMAL_UNUSED);
					exit(1);
				}
				break;
			case 'l':
				max_loadavg = atof(optarg);
				break;
			case 'w':
				use_utmp = 1;
				break;
			case 1:
			case 'H':
				force_hal = 1;
				break;
			case 'i':
				i = atoi(optarg);
				if ((i < 0) || (i >= MAX_IRQS)) {
					fprintf(stderr, "sleepd: bad irq number %d\n", i);
					exit(1);
				}
				irqs[atoi(optarg)] = 1;
				autoprobe = 0;
				have_irqs = 1;
				break;
			case 'e':
				result = access(optarg, R_OK);
				switch(result) {
					case 0:
						strncpy(eventData.events[event], optarg,127);
						use_events = 1;
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
				use_events = 0;
				break;
			case 'a':
				force_autoprobe = 1;
				break;
			case 'I':
				noirq = 1;
				break;
			case 'V':
				printf("%s %d.%d\n"
					"(C) 2000-2008 Joey Hess <joeyh@kitenet.net>\n"
					"(C) 2017 Toni Uhlig <matzeton@googlemail.com>\n\n"
					"This is free software.  You may redistribute copies of it under the terms of\n"
					"the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n"
					"There is NO WARRANTY, to the extent permitted by law.\n\n"
					"Please send me all your bugs: %s\n\n", PKG_NAME, PKG_VERSION_MAJOR, PKG_VERSION_MINOR, PKG_MAIL);
				exit(0);
			case 'h':
				usage(argv[0]);
				exit(1);
				break;
			case 'c':
				sleep_time = atoi(optarg);
				if (sleep_time <= 0) {
					fprintf(stderr, "sleepd: bad sleep time %d\n", sleep_time);
					exit(1);
				}
				break;
			case 'b':
				min_batt = atoi(optarg);
				if (min_batt < 0) {
					fprintf(stderr, "sleepd: bad minimumn battery percentage %d\n", min_batt);
					exit(1);
				}
				break;
			case 'N':
				if (netcount >= MAX_NET) {
					fprintf(stderr, "sleepd: only %d net devices allowed (check '-N' args)\n", MAX_NET);
					exit(1);
				}
				memset(&tmpdev[0], '\0', ARRAY_SIZE(tmpdev));
				strncpy(tmpdev, optarg, 8);
				snprintf(tx_statfile, ARRAY_SIZE(tx_statfile), TXFILE, tmpdev);
				snprintf(rx_statfile, ARRAY_SIZE(rx_statfile), RXFILE, tmpdev);
				if ((access(tx_statfile, R_OK) == 0) &&
				    (access(rx_statfile, R_OK) == 0)) {
					memset(&netdevtx[netcount][0], '\0', ARRAY_SIZE(netdevtx[netcount]));
					memset(&netdevrx[netcount][0], '\0', ARRAY_SIZE(netdevrx[netcount]));
					strncpy(netdevtx[netcount], tx_statfile, 44);
					strncpy(netdevrx[netcount], rx_statfile, 44);
					use_net = 1;
					netcount++;
				} else {
					fprintf(stderr, "sleepd: %s not found in sysfs\n", tmpdev);
					exit(1);
				}
				break;
			case 't':
				if (netcount > 0 && tx_set[netcount-1] == 0) {
					min_tx[netcount-1] = atoi(optarg);
					tx_set[netcount-1] = 1;
				} else {
					fprintf(stderr, "sleepd: you can use '-%c' only ONCE and AFTER the corresponding '-N'\n", 't');
					exit(1);
				}
				break;
			case 'r':
				if (netcount > 0 && rx_set[netcount-1] == 0) {
					min_rx[netcount-1] = atoi(optarg);
					rx_set[netcount-1] = 1;
				} else {
					fprintf(stderr, "sleepd: you can use '-%c' only ONCE and AFTER the corresponding '-N'\n", 'r');
					exit(1);
				}
				break;
			case 'm':
				if (netcount > 0 && sm_set[netcount-1] == 0) {
					net_samples[netcount-1] = atoi(optarg);
					if (net_samples[netcount-1] <= 1 || net_samples[netcount-1] > MAX_SAMPLES) {
						fprintf(stderr, "sleepd: net samples should be between 2 and %d\n", MAX_SAMPLES);
						exit(1);
					}
					sm_set[netcount-1] = 1;
				} else {
					fprintf(stderr, "sleepd: you can use '-%c' only ONCE and AFTER the corresponding '-N'\n", 'm');
					exit(1);
				}
				break;
			case 'A':
				require_unused_and_battery = 1;
				break;
			case 'x':
#ifdef X11
				xmax_unused = atoi(optarg);
				if (xmax_unused < MINIMAL_UNUSED) {
					fprintf(stderr, "sleepd: bad x11 max unused time %d < %d\n", xmax_unused, MINIMAL_UNUSED);
					exit(1);
				}
#else
				fprintf(stderr, "sleepd: x11 idle check disabled\n");
#endif
				break;
			case 'X':
#ifdef X11
				use_xdiff = (unsigned int)atoi(optarg);
#else
				fprintf(stderr, "sleepd: x11 diff check disabled\n");
#endif
				break;
			case 2:
#ifdef X11
				xdiff_max_unused = atoi(optarg);
#else
				fprintf(stderr, "sleepd: x11 diff check disabled\n");
#endif
				break;
			case 'g':
				{
					struct group *grp = getgrnam(optarg);
					if (!grp) {
						perror("getgrnam");
						exit(1);
					}
					shm_grp = grp->gr_gid;
				}
				break;
			default:
				break;
		}
	}
	if (optind < argc) {
		usage(argv[0]);
		exit(1);
	}

	if (use_events) {
		strncpy(eventData.events[event], "", 1);
	}

	if (use_net) {
		strncpy(netdevtx[netcount], "", 1);
		strncpy(netdevrx[netcount], "", 1);
	}

	if (noirq) {
		autoprobe = 0;
	}

	if (force_autoprobe) {
		autoprobe = 1;
	}

#ifdef X11
	if (xdiff_max_unused <= 0) {
		xdiff_max_unused = max_unused;
	}
#endif
}

/**** stat the device file to get an idle time */
// Copied from w.c in procps by Charles Blake
int idletime (const char *tty) {
	struct stat sbuf;
	if (stat(tty, &sbuf) != 0)
		return 0;
	return (int)(time(NULL) - sbuf.st_atime);
}

unsigned char check_irqs (unsigned char activity, unsigned char autoprobe) {
	static long irq_count[MAX_IRQS]; /* holds previous counters of the irqs */
	static int probed = 0;
	static int no_dev_warned = 0;

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
		int do_this_one = 0;
		if (autoprobe) {
			/* Lowercase line. */
			for(i = 0; line[i]; i++)
				line[i] = tolower(line[i]);
			/* See if it is a keyboard or mouse. */
			if (strstr(line, "mouse") != NULL ||
			    strstr(line, "keyboard") != NULL ||
			    /* 2.5 kernels report by chipset,
			     * this is a ps/2 keyboard/mouse. */
			    strstr(line, "i8042") != NULL) {
				do_this_one = 1;
				probed = 1;
			}
		}
		if (sscanf(line,"%d: %ld",&i, &v) == 2 &&
		    i < MAX_IRQS && i >= 0 &&
		    (do_this_one || irqs[i]) && irq_count[i] != v) {
			if (debug)
				printf("sleepd: activity: irq %d\n", i);
			activity = 1;
			irq_count[i] = v;
		}
	}
	fclose(f);
	
	if (autoprobe && ! probed) {
		if (! no_dev_warned) {
			no_dev_warned = 1;
			syslog(LOG_WARNING, "no keyboard or mouse irqs autoprobed");
		}
	}

	return activity;
}

unsigned char check_net (unsigned char activity) {
	static long tx_count[MAX_NET]; /* holds previous counters of tx packets */
	static long rx_count[MAX_NET]; /* holds previous counters of rx packets */

	long tx, rx;
	int i;
	for (i=0; i < MAX_NET; i++) {
		if (strncmp(netdevtx[i], "", 1) != 0) {
			char line[64];
			FILE *f = fopen(netdevtx[i], "r");
			if (fgets(line,sizeof(line),f)) {
				tx = strtol(line, (char **) NULL, 10);
			}
			else {
				fprintf(stderr, "sleepd: could not read %s\n", netdevtx[i]);
				exit(1);
			}
			fclose(f);
			f = fopen(netdevrx[i], "r");
			if (fgets(line,sizeof(line),f)) {
				rx = strtol(line, (char **) NULL, 10);
			}
			else {
				fprintf(stderr, "sleepd: could not read %s\n", netdevrx[i]);
				exit(1);
			}
			fclose(f);

			if (net_samples[i] > 1) {
				net_samples_tx[i][net_samples_idx] = (tx - tx_count[i])/sleep_time;
				net_samples_rx[i][net_samples_idx] = (rx - rx_count[i])/sleep_time;
				if (++net_samples_idx >= net_samples[i]) {
					net_samples_idx = 0;
				}

				long sum_tx = 0;
				long sum_rx = 0;
				size_t j = 0;
				for (j = 0; j < net_samples[i]; ++j) {
					sum_tx += net_samples_tx[i][j];
					sum_rx += net_samples_rx[i][j];
				}
				long avg_tx = sum_tx/net_samples[i];
				long avg_rx = sum_rx/net_samples[i];

				if ((avg_tx > min_tx[i]) || avg_rx > min_rx[i]) {
					if (debug) {
						printf("sleepd: activity: avg network rx/tx rate calculation (%d samples): txrate: %ld rxrate: %ld\n", net_samples[i], avg_tx, avg_rx);
					}
					activity = 1;
				}
			} else
			if (((tx - tx_count[i])/sleep_time > min_tx[i]) ||
			    ((rx - rx_count[i])/sleep_time > min_rx[i])) {
				if (debug) {
					printf("sleepd: activity: network txrate: %ld rxrate: %ld\n",
						(tx - tx_count[i])/sleep_time, (rx - rx_count[i])/sleep_time);
				}
				activity = 1;
			}
			tx_count[i] = tx;
			rx_count[i] = rx;
		} else {
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
	int min_idle = 2 * max_unused;
	utmpname(UTMP_FILE);
	setutent();
	while ((u = getutent())) {
		if (u->ut_type == USER_PROCESS) {
			/* get tty. From w.c in procps by Charles Blake. */
			char tty[5 + sizeof u->ut_line + 1] = "/dev/";
			unsigned i;
			for (i=0; i < sizeof u->ut_line; i++) {
				/* clean up tty if garbled */
				if (isalnum(u->ut_line[i]) ||
				    (u->ut_line[i] == '/')) {
					tty[i+5] = u->ut_line[i];
				}
				else {
					tty[i+5] = '\0';
				}
			}
			int cur_idle = idletime(tty);
			min_idle = (cur_idle < min_idle) ? cur_idle : min_idle;
		}
	}
	/* The shortest idle time is the real idle time */
	total_unused = (min_idle < total_unused) ? min_idle : total_unused;
	if (debug && total_unused == min_idle)
		printf("sleepd: activity: utmp %d seconds\n", min_idle);

	return total_unused;
}

char *safe_env (const char *name)
{
	if (! name)
		return NULL;

	const char *env = getenv(name);
	if (! env)
		return NULL;

	const unsigned max_envlen = ENV_LEN;
	char newenv[max_envlen+1];
	memset(&newenv[0], '\0', ARRAY_SIZE(newenv));
	strncpy(&newenv[0], env, max_envlen);

	size_t env_len = strnlen(&newenv[0], max_envlen);
	size_t nme_len = strnlen(name, max_envlen);
	char *ret = calloc(nme_len + env_len + 2, sizeof(char)); /* +2 for '=' and '\0' e.g. NAME=VALUE */

	strncat(ret, name, nme_len);
	strcat(ret, "=");
	strncat(ret, env, env_len);
	return ret;
}

int safe_exec (const char *cmdWithArgs, int total_unused)
{
	if (!cmdWithArgs)
		return -2;
	pid_t child;
	if ( (child = fork()) == 0 ) {

		int szCur = 0, szMax = 10;
		char **args = calloc(szMax, sizeof(char *));
		const char *cmd = NULL;
		const char *prv = cmdWithArgs;
		const char *cur = NULL;

		char s_unused[11];
		if (snprintf(&s_unused[0], sizeof(s_unused)/sizeof(s_unused[0]), "%d", total_unused) > 0) {
			setenv("SLEEPD_UNUSED", &s_unused[0], 1);
		} else unsetenv("SLEEPD_UNUSED");
		/* prepend enviroment variables for execve */
		const char *const envnames[] = { "SLEEPD_XUSER", "SLEEPD_UNUSED", "DISPLAY", "XAUTHORITY", NULL };
		const unsigned maxEnv = ARRAY_SIZE(envnames);
		char *envp[maxEnv];
		unsigned i = 0, j = 0;

		/* parse env */
		memset(&envp[0], '\0', ARRAY_SIZE(envp) * sizeof(char *));
		do {
			char *tmp = safe_env(envnames[i]);
			if (tmp) {
				envp[j] = strndup(tmp, ENV_LEN);
				free(tmp);
				j++;
			}
		} while (++i < maxEnv);

		/* parse args */
		while ( (cur = strchr(prv, ' ')) ) {
			if (cmd == NULL)
				cmd = strndup(prv, cur-prv);

			/* make space at least for two new elements (strndup + \0) */
			if (szCur >= szMax-2) {
				szMax *= 2;
				args = realloc(args, sizeof(char *) * szMax);
			}
			args[szCur++] = strndup(prv, cur-prv);

			cur++;
			prv = cur;
		}

		if (cmd == NULL) {
			cmd = cmdWithArgs;
			args[0] = strndup(cmd, ENV_LEN);
			szCur++;
		} else {
			args[szCur++] = strndup(prv, cur-prv);
		}
		args[szCur] = NULL;

		errno = 0;
		if (execve(cmd, args, envp) != 0)
			perror("execve");
		exit(-3);
	} else if (child != -1) {
		int retval = 0;
		waitpid(child, &retval, 0);
		return retval;
	}
	return -4;
}

void main_loop (void) {
	unsigned char activity = 0, sleep_now = 0;
        int total_unused = 0;
#ifdef X11
	char xauthority[IPC_PATHMAX+1];
	char xdisplay[IPC_XDISPMAX+1];
	int x_unused = 0;
	unsigned int x_bounds[4];
	XImage *x_oldimg = NULL;
	int xdiff_unused = 0;
#endif
	int sleep_battery = 0;
	int prev_ac_line_status = -1;
	time_t nowtime, oldtime = 0;
	apm_info ai;
	double loadavg[1];

	unsetenv("SLEEPD_XUSER");
	unsetenv("DISPLAY");
	unsetenv("XAUTHORITY");

	memset(&ai, '\0', sizeof(ai));
#ifdef X11
	memset(&x_bounds[0], '\0', sizeof(x_bounds));
#endif

	if (use_events) {
		pthread_create(&emthread, NULL, eventMonitor, NULL);
	}

	while (1) {
		if (ipc_lock() == 0) {
			struct ipc_data *id_ptr = NULL;
			ipc_getshmptr(&id_ptr);
			if (id_ptr != NULL) {
				no_sleep = (GET_FLAG(id_ptr, FLG_ENABLED) == 0);
#ifdef X11
				if (GET_FLAG(id_ptr, FLG_USEX11) != 0) {
					memset(&xauthority[0], '\0', ARRAY_SIZE(xauthority));
					memset(&xdisplay[0], '\0', ARRAY_SIZE(xdisplay));
					strncpy(&xauthority[0], &id_ptr->xauthority[0], IPC_PATHMAX);
					strncpy(&xdisplay[0], &id_ptr->xdisplay[0], IPC_XDISPMAX);
					setenv("XAUTHORITY", &xauthority[0], 1);
					setenv("DISPLAY", &xdisplay[0], 1);

					if (!use_x) {
						if (debug) {
							printf("sleepd: x11 idle check enabled (DISPLAY: %s , XAUTHORITY: %s)\n", &xdisplay[0], &xauthority[0]);
						}
						struct stat st;
						if (stat(&xauthority[0], &st) == 0) {
							struct passwd *pwd = NULL;
							if ((pwd = getpwuid(st.st_uid)) != NULL) {
								setenv("SLEEPD_XUSER", pwd->pw_name, 1);
								if (debug) {
									printf("sleepd: xauth owner: %s\n", pwd->pw_name);
								}
							}
						}
						if (init_x11() != 0) {
							syslog(LOG_ERR, "X11 init failed.\n");
							UNSET_FLAG(id_ptr, FLG_USEX11);
						}
						if (use_xdiff && check_x11_bounds(x_bounds) != 0) {
							syslog(LOG_ERR, "X11 diff using default bounds.\n");
						}
					}

					if (use_xdiff && memcmp(&x_bounds[0], &id_ptr->xdiff_bounds[0], sizeof(x_bounds)) != 0) {
						memcpy(&x_bounds[0], &id_ptr->xdiff_bounds[0], sizeof(x_bounds));
						if (check_x11_bounds(x_bounds) != 0) {
							syslog(LOG_ERR, "X11 bounds check failed, using default.\n");
							memcpy(&id_ptr->xdiff_bounds[0], &x_bounds[0], sizeof(x_bounds));
						}
						if (x_oldimg) {
							XDestroyImage(x_oldimg);
							x_oldimg = NULL;
						}
						if (debug) {
							printf("sleepd: X11 bounds: x = %u , y = %u , w = %u , h = %u\n", x_bounds[0], x_bounds[1], x_bounds[2], x_bounds[3]);
						}
					}
				}
				else if (use_x) {
					memset(&id_ptr->xauthority[0], '\0', IPC_PATHMAX);
					memset(&id_ptr->xdisplay[0], '\0', IPC_XDISPMAX);
					unsetenv("DISPLAY");
					unsetenv("XAUTHORITY");
					unsetenv("SLEEPD_XUSER");
					memset(&x_bounds[0], '\0', sizeof(x_bounds));
				}
				use_x = GET_FLAG(id_ptr, FLG_USEX11) != 0;
#endif
			}
			ipc_unlock();
		}

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
#ifdef X11
		if (use_x) {
			x_unused = check_x11();
			if (x_unused == -1) {
				syslog(LOG_ERR, "X11 idle check failed, disable.\n");
				use_x = 0;
			}
			if (x_unused == 0)
				activity=1;
		}
		if (use_xdiff) {
			ssize_t ret = calc_x11_screendiff(&x_oldimg, x_bounds, use_xdiff+1);
			if (ret >= 0) {
				if (ret <= use_xdiff) {
					xdiff_unused += sleep_time;
				} else xdiff_unused = 0;
				if (debug)
					printf("sleepd: x11 diff returned %lu\n", ret);
			}
 		}
#endif

		if (debug && ai.battery_status != BATTERY_STATUS_ABSENT)
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
			if (safe_exec(hibernate_command, total_unused) != 0)
				syslog(LOG_ERR, "%s failed", hibernate_command);
			/* This counts as activity; to prevent double sleeps. */
			if (debug)
				printf("sleepd: activity: just woke up\n");
			activity = 1;
			oldtime = 0;
			sleep_battery = 0;
		}

		if ((ai.ac_line_status != prev_ac_line_status) && (prev_ac_line_status != -1)) {
			/* AC plug/unplug counts as activity. */
			if (debug)
				printf("sleepd: activity: AC status change\n");
			activity = 1;
		}
		prev_ac_line_status = ai.ac_line_status;

		/* Rest is only needed if sleeping on inactivity. */
		if (! max_unused && ! ac_max_unused) {
			sleep(sleep_time);
			continue;
		}

		if (autoprobe || have_irqs) {
			activity = check_irqs(activity, autoprobe);
		}

		if (use_net) {
			activity = check_net(activity);
		}

		if ((max_loadavg != 0) &&
		    (getloadavg(loadavg, 1) == 1) &&
		    (loadavg[0] >= max_loadavg)) {
			/* If the load average is too high */
			if (debug)
				printf("sleepd: activity: load average %f\n", loadavg[0]);
			activity = 1;
		}

		if (use_utmp == 1) {
			total_unused = check_utmp(total_unused);
		}

		if (ipc_lock() == 0) {
			struct ipc_data *id_ptr = NULL;
			ipc_getshmptr(&id_ptr);
			id_ptr->total_unused = total_unused;
#ifdef X11
			id_ptr->xmax_unused = x_unused;
			id_ptr->xdiff_unused = xdiff_unused;
#endif
			ipc_unlock();
		}

		sleep(sleep_time);

#ifdef X11
		if (use_x && ! no_sleep) {
			if (xmax_unused > 0) {
				sleep_now = (x_unused >= xmax_unused);
			} else if (max_unused > 0) {
				sleep_now = (x_unused >= max_unused);
			}
			if (sleep_now) {
				syslog(LOG_NOTICE, "x11 inactive");
				activity = 0;
				total_unused = x_unused;
			}
			if (use_xdiff && ! sleep_now) {
				sleep_now = (xdiff_unused > xdiff_max_unused);
				if (sleep_now) {
					syslog(LOG_NOTICE, "x11 diff inactive");
					activity = 0;
					total_unused = xdiff_unused;
				}
			}
		}
#endif

		if (use_events) {
			pthread_mutex_lock(&activity_mutex);
			int i;
			for (i=0; eventData.channels[i] != -1; i++) {
				if (eventData.emactivity[i] == 1) {
					if (debug)
						printf("sleepd: activity: keyboard/mouse events %s\n", eventData.events[i]);
					activity = 1;
#ifdef X11
					xdiff_unused = 0;
#endif
				}
			}
			pthread_mutex_unlock(&activity_mutex);
		}

		if (activity) {
			total_unused = 0;
		}
		else {
			total_unused += sleep_time;
			if (! sleep_now && ai.ac_line_status == 1) {
				/* On wall power. */
				if (ac_max_unused > 0) {
					sleep_now = (total_unused >= ac_max_unused);
				}
			}
			else if (! sleep_now && max_unused > 0) {
				sleep_now = (total_unused >= max_unused);
			}

			if (sleep_now && ! no_sleep && ! require_unused_and_battery) {
				syslog(LOG_NOTICE, "system inactive for %ds; forcing sleep", total_unused);
				if (safe_exec(sleep_command, total_unused) != 0) {
					syslog(LOG_ERR, "%s failed", sleep_command);
				}
				total_unused = 0;
#ifdef X11
				x_unused = 0;
				xdiff_unused = 0;
#endif
				oldtime = 0;
				sleep_now = 0;
			}
			else if (sleep_now && ! no_sleep && sleep_battery) {
				syslog(LOG_NOTICE, "system inactive for %ds and battery level %d%% is below %d%%; forcing hibernaton", 
				       total_unused, ai.battery_percentage, min_batt);
				if (safe_exec(hibernate_command, total_unused) != 0) {
					syslog(LOG_ERR, "%s failed", hibernate_command);
				}
				total_unused = 0;
#ifdef X11
				x_unused = 0;
#endif
				oldtime = 0;
				sleep_now = 0;
				sleep_battery = 0;
			}
		}

		/*
		 * Keep track of how long it's been since we were last
		 * here. If it was much longer than sleep_time, the system
		 * was probably suspended, or this program was, (or the 
		 * kernel is thrashing :-), so clear idle counter.
		 */
		nowtime = time(NULL);
		/* The 1 is a necessary fudge factor. */
		if (oldtime && (nowtime - sleep_time) > (oldtime + 1)) {
			no_sleep = 0; /* reset, since they must have put it to sleep */
			syslog(LOG_NOTICE,
					"%i sec sleep; resetting timer",
					(int)(nowtime - oldtime));
			total_unused = 0;
		}
		oldtime = nowtime;
	}
}

void cleanup (int signum) {
	if (daemonize) {
		unlink(PID_FILE);
	}
	ipc_close_master();
	if (use_events && pthread_cancel(emthread) == 0)
		pthread_join(emthread, NULL);
	exit(0);
}

int main (int argc, char **argv) {
	FILE *f;

	memset(&irqs[0], '\0', MAX_IRQS*sizeof(int));
	size_t i;
	for (i = 0; i < MAX_NET; ++i) {
		min_tx[i] = TXRATE;
		min_rx[i] = RXRATE;
		net_samples[i] = 1;
		memset(&netdevtx[i][0], '\0', ARRAY_SIZE(netdevtx[i]));
		memset(&netdevrx[i][0], '\0', ARRAY_SIZE(netdevrx[i]));
	}
	memset(&net_samples_rx[0], '\0', MAX_NET*MAX_SAMPLES*sizeof(int));
	memset(&net_samples_tx[0], '\0', MAX_NET*MAX_SAMPLES*sizeof(int));

	parse_command_line(argc, argv);

	/* Log to the console if not daemonizing. */
	openlog("sleepd", LOG_PID | (daemonize ? 0 : LOG_PERROR), LOG_DAEMON);

	/* Set up a signal handler for SIGTERM/SIGINT to clean up things. */
	signal(SIGTERM, cleanup);
	if (! daemonize)
		signal(SIGINT, cleanup);

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
		if ((f = fopen(PID_FILE, "w")) == NULL) {
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
			sleep_command = acpi_sleep_command;

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
			use_acpi = 1;
		}
#if defined(HAL)
		else if (simplehal_supported()) {
			use_simplehal = 1;
		}
		else {
			syslog(LOG_NOTICE, "failed to connect to hal on startup, but will try to use it anyway");
			use_simplehal = 1;
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
		sleep_command = apm_sleep_command;
#else
		sleep_command = acpi_sleep_command;
#endif
	}
	if (! hibernate_command) {
		hibernate_command = sleep_command;
	}

	if (ipc_init_master(shm_grp) != 0) {
		perror("ipc_init");
		exit(1);
        }
	main_loop();

	return(0); // never reached
}
