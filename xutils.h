/*
 * An X11 Xlib interface for sleepd (matzeton@googlemail.com)
 * (not Threadsafe!)
 */

#include <sys/types.h>
#include <X11/Xlib.h>

extern int init_x11(void);
extern int close_x11(void);
extern int check_x11 (void);
extern ssize_t calc_x11_screendiff(XImage **xold_ptr, unsigned int bounds[4], unsigned int maxdiff);
extern int check_x11_bounds(unsigned int xdiff_bounds[4]);
