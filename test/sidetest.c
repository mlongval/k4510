/* test/sidetest.c -- libk4510side.so as a personality loads it: dlopen, the
 * four names and nothing else, every scene draws something, an unknown scene
 * and the Navidrome one are refused, and the ant farm's colony goes to its
 * state file and comes back. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "k4510side.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("sidetest: FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(int argc, char **argv)
{
    void *h = dlopen(argc > 1 ? argv[1] : "sdl/libk4510side.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("sidetest: FAIL dlopen: %s\n", dlerror()); return 1; }
    int  (*api)(void) = (int (*)(void)) dlsym(h, "k4510side_api");
    int  (*init)(const char *) = (int (*)(const char *)) dlsym(h, "k4510side_init");
    void (*draw)(uint32_t *, int, int, int, uint32_t) = (void (*)(uint32_t *, int, int, int, uint32_t)) dlsym(h, "k4510side_draw");
    void (*quit)(void) = (void (*)(void)) dlsym(h, "k4510side_quit");
    if (!api || !init || !draw || !quit) { printf("sidetest: FAIL a name is missing\n"); return 1; }
    CHECK(api() == K4510SIDE_API, "api %d", api());
    CHECK(dlsym(h, "s_space") == NULL && dlsym(h, "saver_draw") == NULL, "the scenes' own names leak out");
    const char *scenes[] = { "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm", "matrix" };
    enum { W = 160, H = 270, P = 176 };
    static uint32_t px[P * H];
    for (size_t i = 0; i < sizeof scenes / sizeof *scenes; i++) {
        CHECK(init(scenes[i]) == 0, "init %s", scenes[i]);
        memset(px, 0, sizeof px);
        for (uint32_t t = 0; t < 2000; t += 40) draw(px, W, H, P, t);
        int lit = 0, outside = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < P; x++) {
            if (x < W && (px[y * P + x] & 0xFFFFFF)) lit++;
            if (x >= W && px[y * P + x]) outside++;
        }
        CHECK(lit > W * H / 20, "%s drew %d lit pixels", scenes[i], lit);
        CHECK(outside == 0, "%s drew past w, into the pitch", scenes[i]);
        quit();
    }
    CHECK(init("navidrome") == -1, "navidrome accepted");
    CHECK(init("nosuch") == -1, "an unknown scene accepted");
    CHECK(init(NULL) == -1, "NULL accepted");
    draw(px, W, H, P, 0);                          /* no scene: nothing, no crash */

    char dir[] = "/tmp/sidetestXXXXXX", cfg[128], f[160];
    CHECK(mkdtemp(dir) != NULL, "mkdtemp");
    snprintf(cfg, sizeof cfg, "antfarm day=30m state=%s", dir);
    snprintf(f, sizeof f, "%s/antfarm.DAT", dir);
    CHECK(init(cfg) == 0, "init %s", cfg);
    for (uint32_t t = 0; t < 60000; t += 40) draw(px, W, H, P, t);
    quit();
    CHECK(access(f, R_OK) == 0, "no %s after quit", f);
    CHECK(init(cfg) == 0, "init again");
    draw(px, W, H, P, 0);
    quit();
    unlink(f); rmdir(dir);
    dlclose(h);
    if (!fails) printf("sidetest: libk4510side.so -- 8 scenes, state kept, nothing leaks\n");
    return fails != 0;
}
