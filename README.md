sleepd is a daemon to to put a machine to sleep if it is not being used or if the battery is low (if present).
It can be controlled by sleepctl via POSIX IPC.

It supports HAL, APM, and ACPI, although external programs must be used to actually put the system to sleep.

This is a debian/jessie fork (sleepd-2.08). The origin of this project: https://joeyh.name/code/sleepd
The goal is to fix some bugs and provide a modern ipc interface and X11 support (optional).
See manpages for more info.
