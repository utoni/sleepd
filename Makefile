CFLAGS     ?= -O2 -g -Wall
CFLAGS     += -DACPI_APM -pthread
BINS        = sleepd sleepctl
PREFIX      = /
INSTALL_PROGRAM	= install
# USE_HAL		= 1
# USE_APM		= 1
# USE_UPOWER	= 1

# DEB_BUILD_OPTIONS suport, to control binary stripping.
ifeq (,$(findstring nostrip,$(DEB_BUILD_OPTIONS)))
INSTALL_PROGRAM += -s
endif

# And debug building.
ifneq (,$(findstring debug,$(DEB_BUILD_OPTIONS)))
CFLAGS += -g
endif

OBJS=sleepd.o acpi.o eventmonitor.o
LIBS=-lpthread

all: $(BINS)

ifdef USE_HAL
LIBS+=$(shell pkg-config --libs hal)
OBJS+=simplehal.o
CFLAGS+=-DHAL
simplehal.o: simplehal.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell pkg-config --cflags hal) -c simplehal.c -o simplehal.o
endif

ifdef USE_UPOWER
LIBS+=$(shell pkg-config --libs upower-glib)
OBJS+=upower.o
CFLAGS+=-DUPOWER
upower.o: upower.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell pkg-config --cflags upower-glib) -c upower.c -o upower.o
endif

ifdef USE_APM
LIBS+=-lapm
CFLAGS+=-DUSE_APM
endif

ifndef USE_APM
CFLAGS+=-DACPI_APM
endif

sleepd: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f $(BINS) *.o

install: $(BINS)
	install -d $(PREFIX)/usr/sbin/ $(PREFIX)/usr/share/man/man8/ \
		$(PREFIX)/usr/bin/ $(PREFIX)/usr/share/man/man1/
	$(INSTALL_PROGRAM) sleepd $(PREFIX)/usr/sbin/
	install -m 0644 sleepd.8 $(PREFIX)/usr/share/man/man8/
	$(INSTALL_PROGRAM) -m 4755 -o root -g root sleepctl $(PREFIX)/usr/bin/
	install -m 0644 sleepctl.1 $(PREFIX)/usr/share/man/man1/
