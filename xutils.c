#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/extensions/scrnsaver.h>

#include "xutils.h"


static Display *display = NULL;
static Window root;
static int max_width, max_height;


int init_x11(void) {
	display = XOpenDisplay(NULL);
	if (!display)
		return -1;
	root = DefaultRootWindow(display);
	XWindowAttributes attr;
	if (XGetWindowAttributes(display, root, &attr) == 0) {
		close_x11();
		return -1;
	}
	max_width = attr.width;
	max_height = attr.height;

	return (display != NULL ? 0 : 1);
}

int close_x11(void) {
	if (!display)
		return -1;
	int ret = XCloseDisplay(display);
	memset(&root, '\0', sizeof(Window));
	max_width = max_height = 0;
	display = NULL;
	return ret;
}

int check_x11 (void) {
	int event_base, error_base;
	XScreenSaverInfo info;

	if (!display)
		return -1;
	if (XScreenSaverQueryExtension(display, &event_base, &error_base) != 0) {
		if (XScreenSaverQueryInfo(display, DefaultRootWindow(display), &info) == 0) {
			return -1;
		}
	}

	return (int)(info.idle/1000.0f);
}

ssize_t calc_x11_screendiff(XImage **xold_ptr, unsigned int bounds[4], unsigned int maxdiff)
{
	unsigned int diff = 0;
	unsigned int x, y;

	if (!display || !xold_ptr)
		return -1;
	if (! *xold_ptr)
		*xold_ptr = XGetImage(display, root, bounds[0], bounds[1], bounds[2], bounds[3], AllPlanes, ZPixmap);
	XImage *img = XGetImage(display, root, bounds[0], bounds[1], bounds[2], bounds[3], AllPlanes, ZPixmap);
	if (!img)
		return -1;

	for (x = 0; x < bounds[2]; ++x) {
		for (y = 0; y < bounds[3]; ++y) {
			unsigned long newpixel = XGetPixel(img, x, y);
			unsigned long oldpixel = XGetPixel(*xold_ptr, x, y);
			if (newpixel != oldpixel)
				diff++;
			if (diff >= maxdiff)
				goto ret;
		}
	}

ret:
	XDestroyImage(*xold_ptr);
	*xold_ptr = img;
	return diff;
}

static inline int checkBound(unsigned int value, unsigned int max)
{
  return value > max;
}

int check_x11_bounds(unsigned int xdiff_bounds[4])
{
	if (checkBound(xdiff_bounds[0], max_width-1) != 0 ||
		checkBound(xdiff_bounds[1], max_height-1) != 0 ||
		checkBound(xdiff_bounds[2], max_width-xdiff_bounds[0]) != 0 ||
		checkBound(xdiff_bounds[3], max_height-xdiff_bounds[1]) != 0 ||
		xdiff_bounds[2] == 0 || xdiff_bounds[3] == 0) {
			xdiff_bounds[0] = 0;
			xdiff_bounds[1] = 0;
			xdiff_bounds[2] = max_width;
			xdiff_bounds[3] = max_height;
			return 1;
	}
	return 0;
}
