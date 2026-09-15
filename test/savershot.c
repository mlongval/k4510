/* test/savershot.c -- the sidebar-savers (sdl/savers.c) without SDL: each
 * scene at one sidebar size, a frame every STEP ms, as PPM files, for looking
 * at them from a terminal.
 *     cc -O2 -o savershot test/savershot.c sdl/savers.c
 *     ./savershot DIR W H FRAMES STEP     -> DIR/s<n>-<side>-<frame>.ppm */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../sdl/savers.h"
int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: savershot DIR W H FRAMES STEP\n"); return 2; }
    const char *dir = argv[1]; int w = atoi(argv[2]), h = atoi(argv[3]), n = atoi(argv[4]), step = atoi(argv[5]);
    uint32_t *px = malloc((size_t) w * h * 4);
    for (int s = 0; s < SAVER_COUNT; s++) for (int side = 0; side < 2; side++) for (int f = 0; f < n; f++) {
        saver_draw(s, px, w, w, h, (uint32_t)(5000 + f * step), side);
        char path[512]; snprintf(path, sizeof path, "%s/s%d-%d-%03d.ppm", dir, s, side, f);
        FILE *o = fopen(path, "wb"); if (!o) return 1;
        fprintf(o, "P6 %d %d 255\n", w, h);
        for (int i = 0; i < w * h; i++) { fputc(px[i] >> 16 & 255, o); fputc(px[i] >> 8 & 255, o); fputc(px[i] & 255, o); }
        fclose(o);
    }
    free(px); return 0;
}
