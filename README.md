<img src=https://travis-ci.org/lnslbrty/sleepd.svg?branch=master>
<a href="https://scan.coverity.com/projects/lnslbrty-sleepd">
  <img alt="Coverity Scan Build Status" src="https://scan.coverity.com/projects/11901/badge.svg"/>
</a>

sleepd-2.10
========

sleepd is a daemon to to put a machine to sleep if it is not being used or if the battery is low (if present). <br />
It can be controlled by sleepctl via POSIX IPC. <br />

It supports HAL, APM, and ACPI, although external programs must be used to actually put the system to sleep. <br />

This is a debian/jessie fork (sleepd-2.08). The origin of this project: https://joeyh.name/code/sleepd <br />
The goal is to fix some bugs and provide a modern ipc interface and X11 support (optional). <br />
See manpages for more info. <br />
