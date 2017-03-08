CFLAGS     ?= -O2 -g -Wall -Wunused
CFLAGS     += -pthread
BUILDDIR   ?= .
BINS        = $(BUILDDIR)/sleepd $(BUILDDIR)/sleepctl
PREFIX      = /
INSTALL_PROGRAM	= install
# USE_HAL		= 1
# USE_APM		= 1
# USE_UPOWER		= 1
# USE_X11		= 1

# DEB_BUILD_OPTIONS suport, to control binary stripping.
ifeq (,$(findstring nostrip,$(DEB_BUILD_OPTIONS)))
INSTALL_PROGRAM += -s
CFLAGS    += -flto
LDFLAGS   += -flto -Wl,-gc-sections
endif

# And debug building.
ifneq (,$(findstring debug,$(DEB_BUILD_OPTIONS)))
CFLAGS += -g
endif

SLEEPD_OBJS_BUILD=sleepd.o ipc.o acpi.o eventmonitor.o
SLEEPD_LIBS=-lpthread -lrt

SLEEPCTL_OBJS_BUILD=sleepctl.o ipc.o
SLEEPCTL_LIBS=-lpthread -lrt

all: $(BINS)

$(BUILDDIR)/.pre-build:
	mkdir -p $(BUILDDIR) $(BUILDDIR)/sleepd-objs $(BUILDDIR)/sleepctl-objs
	touch $@

ifdef USE_HAL
SLEEPD_LIBS+=$(shell pkg-config --libs hal)
SLEEPD_OBJS+=simplehal.o
CFLAGS+=-DHAL
$(BUILDDIR)/sleepd-objs/simplehal.o: simplehal.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell pkg-config --cflags hal) -c simplehal.c -o $@
endif

ifdef USE_UPOWER
SLEEPD_LIBS+=$(shell pkg-config --libs upower-glib)
SLEEPD_OBJS+=upower.o
CFLAGS+=-DUPOWER
$(BUILDDIR)/sleepd-objs/upower.o: upower.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell pkg-config --cflags upower-glib) -c upower.c -o $@
endif

ifdef USE_APM
SLEEPD_LIBS+=-lapm
CFLAGS+=-DUSE_APM
endif

ifndef USE_APM
CFLAGS+=-DACPI_APM
endif

ifdef USE_X11
SLEEPD_LIBS+=-lX11 -lXss
CFLAGS+=-DX11
endif

SLEEPD_OBJS_PREFIX=$(addprefix $(BUILDDIR)/sleepd-objs/,$(SLEEPD_OBJS))
SLEEPD_OBJS_BUILD_PREFIX=$(addprefix $(BUILDDIR)/sleepd-objs/,$(SLEEPD_OBJS_BUILD))
SLEEPCTL_OBJS_PREFIX=$(addprefix $(BUILDDIR)/sleepctl-objs/,$(SLEEPCTL_OBJS_BUILD))

$(SLEEPD_OBJS_BUILD_PREFIX):
	$(CC) $(CFLAGS) $(CPPFLAGS) -DIS_MASTER=1 -c -o $@ $(patsubst %.o,%.c,$(notdir $@))

$(SLEEPCTL_OBJS_PREFIX):
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(patsubst %.o,%.c,$(notdir $@))

$(BUILDDIR)/sleepd: $(BUILDDIR)/.pre-build $(SLEEPD_OBJS_PREFIX) $(SLEEPD_OBJS_BUILD_PREFIX)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SLEEPD_OBJS_PREFIX) $(SLEEPD_OBJS_BUILD_PREFIX) $(SLEEPD_LIBS)

$(BUILDDIR)/sleepctl: $(BUILDDIR)/.pre-build $(SLEEPCTL_OBJS_PREFIX)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SLEEPCTL_OBJS_PREFIX) $(SLEEPCTL_LIBS)

clean:
	rm -f $(BUILDDIR)/.pre-build
	rm -f $(BUILDDIR)/sleepd $(BUILDDIR)/sleepctl
	rm -f $(BUILDDIR)/sleepd-objs/*.o $(BUILDDIR)/sleepctl-objs/*.o
	rmdir $(BUILDDIR)/sleepd-objs $(BUILDDIR)/sleepctl-objs 2>/dev/null || true
	rmdir $(BUILDDIR) 2>/dev/null || true

install: $(BINS)
	install -d $(PREFIX)/usr/sbin/ $(PREFIX)/usr/share/man/man8/ \
		$(PREFIX)/usr/bin/ $(PREFIX)/usr/share/man/man1/
	$(INSTALL_PROGRAM) $(BUILDDIR)/sleepd $(PREFIX)/usr/sbin/
	install -m 0644 sleepd.8 $(PREFIX)/usr/share/man/man8/
	$(INSTALL_PROGRAM) $(BUILDDIR)/sleepctl $(PREFIX)/usr/bin/
	install -m 0644 sleepctl.1 $(PREFIX)/usr/share/man/man1/

.PHONY: all clean
