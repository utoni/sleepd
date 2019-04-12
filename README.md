Master: <img src="https://travis-ci.org/lnslbrty/sleepd.svg?branch=master">
Debian: <img src="https://travis-ci.org/lnslbrty/sleepd.svg?branch=debian">
<a href="https://scan.coverity.com/projects/lnslbrty-sleepd">
  <img alt="Coverity Scan Build Status" src="https://scan.coverity.com/projects/11901/badge.svg" />
</a>
<img alt="Codacy certification" src="https://api.codacy.com/project/badge/Grade/aa2041f1e6e648ae83945d29cfa0da17" />
<img alt="Github Issues" src="https://img.shields.io/github/issues/lnslbrty/sleepd.svg" />
<img alt="License" src="https://img.shields.io/github/license/lnslbrty/sleepd.svg" />

sleepd
========

sleepd is a daemon to to put a machine to sleep if it is not being used or if the battery is low (if present). <br />
It can be controlled by sleepctl via POSIX IPC. <br />

It supports HAL, APM, and ACPI, although external programs must be used to actually put the system to sleep. <br />

This is a debian/jessie fork (sleepd-2.08). The origin of this project: https://joeyh.name/code/sleepd <br />
The goal is to fix some bugs and provide a modern ipc interface and X11 support (optional). <br />
See manpages for more info. <br />
