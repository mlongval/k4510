/* sdl/k4510side.c -- libk4510side.so: the sidebar scenes for the
 * personalities (sdl/k4510side.h says how it is used).  It only picks a scene,
 * passes its options, and keeps its state in a file; sdl/savers.c and
 * sdl/sidebars/ draw, the same code as in the K4510. */
#include "k4510side.h"
#include "savers.h"
#include "sidebars/canvas.h"
#include <stdio.h>
#include <strings.h>

static const char *const names[] = { "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm", "matrix" };
static const int which_of[] = { SAVER_HALLOWEEN, SAVER_CHRISTMAS, SAVER_SPACE, SAVER_RIVER, SAVER_DREAMFALL, SAVER_TETRIS, SAVER_ANTFARM, SAVER_MATRIX };
static int which = -1;
static char statefile[512];

/* the Navidrome scene stays in the K4510 (its music goes into the machine's
 * sound); savers.c still names it, so it is a blank here */
void s_navidrome(cv_t *c, uint32_t t, int side) { (void) t; (void) side; for (int y = 0; y < c->h; y++) for (int x = 0; x < c->w; x++) c->px[y * c->pitch + x] = 0xFF000000u; }
void navi_option(const char *key, const char *value) { (void) key; (void) value; }

int k4510side_api(void) { return K4510SIDE_API; }

int k4510side_init(const char *config)
{
    char buf[1024], *tok, *save = NULL;
    which = -1; statefile[0] = 0;
    if (!config) return -1;
    snprintf(buf, sizeof buf, "%s", config);
    tok = strtok_r(buf, " \t,", &save);
    for (size_t i = 0; tok && i < sizeof names / sizeof *names; i++)
        if (!strcasecmp(tok, names[i])) which = which_of[i];
    if (which < 0) return -1;
    char name[32]; snprintf(name, sizeof name, "%s", tok);
    while ((tok = strtok_r(NULL, " \t,", &save))) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(tok, "state")) snprintf(statefile, sizeof statefile, "%s/%s.DAT", eq + 1, name);
        else saver_option(which, tok, eq + 1);
    }
    if (statefile[0]) {                                   /* what the scene kept last time */
        FILE *f = fopen(statefile, "rb");
        if (f) {
            uint8_t *st = NULL; long n;
            if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
                && (st = malloc((size_t) n)) && fread(st, 1, (size_t) n, f) == (size_t) n)
                saver_restore(which, st, (size_t) n);
            free(st); fclose(f);
        }
    }
    return 0;
}

void k4510side_draw(uint32_t *px, int w, int h, int pitch, uint32_t ms)
{
    if (which >= 0 && px) saver_draw(which, px, pitch, w, h, ms, 1);   /* 1: the right-hand side's scene */
}

void k4510side_quit(void)
{
    uint8_t *st = NULL;
    size_t n;
    if (which >= 0 && statefile[0] && (n = saver_state(which, &st)) > 0) {
        char tmp[520]; snprintf(tmp, sizeof tmp, "%s.tmp", statefile);
        FILE *f = fopen(tmp, "wb");
        if (f) { int ok = fwrite(st, 1, n, f) == n; ok &= fclose(f) == 0; if (ok) rename(tmp, statefile); else remove(tmp); }
    }
    free(st);
    which = -1;
}
