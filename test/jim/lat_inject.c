/* lat_inject -- keys into an X display through XTest, each one's send time
 * logged (CLOCK_MONOTONIC ns), for test/jim/latrun.sh.
 *   lat_inject COUNT MIN_MS MAX_MS LOG     (DISPLAY from the environment)
 * The pointer is parked mid-screen, over the emulator's (centred) window, so a
 * display without a window manager gives that window the keys. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
static unsigned long long mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long) t.tv_sec * 1000000000ull + t.tv_nsec; }
int main(int argc, char **argv)
{
    Display *d; KeyCode k; FILE *lg; int n, lo, hi;
    if (argc < 5) { fprintf(stderr, "usage: lat_inject COUNT MIN_MS MAX_MS LOG\n"); return 2; }
    n = atoi(argv[1]); lo = atoi(argv[2]); hi = atoi(argv[3]);
    if (!(d = XOpenDisplay(NULL)) || !(lg = fopen(argv[4], "w"))) { fprintf(stderr, "lat_inject: no display or log\n"); return 1; }
    srand(4510);
    XTestFakeMotionEvent(d, -1, DisplayWidth(d, 0) / 2, DisplayHeight(d, 0) / 2, 0); XFlush(d);
    k = XKeysymToKeycode(d, XK_a);
    for (int i = 0; i < n; i++) {
        struct timespec s; int ms = lo + (hi > lo ? rand() % (hi - lo) : 0);
        s.tv_sec = ms / 1000; s.tv_nsec = (long)(ms % 1000) * 1000000L; nanosleep(&s, NULL);
        unsigned long long t = mono();
        XTestFakeKeyEvent(d, k, True, 0); XTestFakeKeyEvent(d, k, False, 0); XFlush(d);
        fprintf(lg, "%llu\n", t); fflush(lg);
    }
    XCloseDisplay(d);
    return 0;
}
