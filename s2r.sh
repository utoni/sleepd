#!/bin/bash

NOTIF_TIME=20

if [ $# -eq 1 -a x"$1" = x"force" ]; then
	SLEEP_IMMED=1
else
	SLEEP_IMMED=0
fi

# user gets additional time before sleep if xidle avail
if [ ${SLEEP_IMMED} -eq 0 -a x"$(command -v xidle)" != x ]; then
	xidle >/dev/null 2>/dev/null
	if [ $? -eq 0 ]; then
		# display a notification before sleeping
		sudo -u ${SLEEPD_XUSER} notify-send -t ${NOTIF_TIME}000 "forced sleep in ${NOTIF_TIME}s; ${SLEEPD_UNUSED}s unused"
		sleep ${NOTIF_TIME}
		if [ $(echo $(xidle)'<'"${NOTIF_TIME}".0|bc -l) -eq 1 ]; then
			sudo -u ${SLEEPD_XUSER} notify-send -t 5 "forced sleep manually aborted"
			echo 'forced sleep aborted by user' | logger
			exit 1
		fi
	fi
fi

# lock screen
/usr/bin/xtrlock &

SLEEP_DATE=$(date '+%d.%m.%y - %H:%M:%S')

# go to sleep
/usr/sbin/s2ram --force --no_kms --acpi_sleep 1

# show notify message after sleeping
sudo -u ${SLEEPD_XUSER} notify-send -t ${NOTIF_TIME}000 "wokeup from sleep; since ${SLEEP_DATE}"

exit 0
