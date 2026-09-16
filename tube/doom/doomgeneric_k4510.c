/* doomgeneric_k4510.c -- DOOM as the K4510's Tube co-processor.
 *
 * Doc, 2026-09-16: "go ahead and build the doom tube".
 *
 * Every other Tube program (BBC BASIC, CP/M, the host shell) talks to the
 * machine through the pty: bytes up become the console's text, and the ULA
 * picks the graphics out of OSC escapes (ESC ] K4G; ... BEL).  DOOM cannot
 * work that way.  A 320x200 frame is 64 KB, thirty-five times a second is
 * 2.2 MB/s, and every byte of it would be pushed through a byte-at-a-time
 * escape parser written for VDU codes.
 *
 * So the pixels take their own road: a shared memory segment the emulator
 * creates and names in K4510_DOOM_SHM.  This process mmaps it, writes each
 * finished frame there with a sequence number, and the emulator blits it
 * into VICKY's bitmap at $200000.  The pty stays for what it is good at --
 * the startup message, and anything the engine prints when it cannot find a
 * WAD.
 *
 * Input comes back the same way, and it has to.  doomgeneric wants key-down
 * AND key-up events; a pty delivers keystrokes, never releases, so a player
 * would walk into a wall and stay there.  The emulator writes the keys that
 * are down RIGHT NOW into the segment's header every frame, and this side
 * diffs that mask against the last one to synthesise the presses and the
 * releases.  It is the same idea as the machine's own $D104 (IO_KBDHELD),
 * widened: that register is one byte and six of its bits are spoken for.
 */
#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
/* i_video.h, not a hand-written extern: under -DCMAP256 it declares both
 * `colors[256]' and `palette_changed', and struct color is FOUR BITFIELDS in
 * the order b, g, r, a -- which on a little-endian host is byte b first.  The
 * first draft of this file declared `struct { unsigned char a, r, g, b; }'
 * from reading the code rather than including the header, and it compiled
 * without a murmur: separate translation unit, no check.  Red would have come
 * out green, green red, and blue would have been the alpha byte, which is
 * always zero.  Include the header and let the compiler hold the contract. */
#include "i_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>

/* The shared segment, as both sides see it.  core/io.c has the identical
 * struct; if you change one, change the other -- there is no header they
 * can share, because this program is not built with the emulator. */
#define DOOM_W 320
#define DOOM_H 200
#define DOOM_MAGIC 0x4B344D44u        /* "DM4K" */

struct doom_shm {
    uint32_t magic;
    uint32_t seq;                     /* bumped after each frame is written */
    uint32_t held;                    /* the emulator writes; we read */
    uint32_t quit;                    /* the emulator asks us to stop */
    uint32_t pal_seq;                 /* bumped when the palette below changes */
    uint32_t pad[3];
    uint8_t  pal[256 * 3];            /* R,G,B per entry, as DOOM has them */
    uint8_t  fb[DOOM_W * DOOM_H];     /* one byte a pixel, palette indices */
};

/* The held-key bits.  These are the emulator's; core/io.h names them
 * K4DOOM_*.  Sixteen is enough for DOOM and leaves room. */
#define K4D_FORWARD  0x0001
#define K4D_BACK     0x0002
#define K4D_LEFT     0x0004          /* turn */
#define K4D_RIGHT    0x0008
#define K4D_STRAFEL  0x0010
#define K4D_STRAFER  0x0020
#define K4D_FIRE     0x0040
#define K4D_USE      0x0080
#define K4D_RUN      0x0100
#define K4D_ESCAPE   0x0200
#define K4D_ENTER    0x0400
#define K4D_MAP      0x0800
#define K4D_WEAPUP   0x1000
#define K4D_WEAPDN   0x2000
#define K4D_YES      0x4000          /* y, for the quit prompt */
#define K4D_NO       0x8000

static struct doom_shm *shm;
static uint32_t held_last;

/* A small queue, because one frame can turn several bits at once and
 * doomgeneric takes one event per DG_GetKey call. */
#define QN 64
static struct { int pressed; unsigned char key; } q[QN];
static unsigned qw, qr;
static void push(int pressed, unsigned char key)
{
    if ((unsigned)(qw - qr) >= QN) return;               /* full: drop, never block the frame */
    q[qw % QN].pressed = pressed; q[qw % QN].key = key; qw++;
}

static const struct { uint32_t bit; unsigned char key; } keymap[] = {
    { K4D_FORWARD, KEY_UPARROW },   { K4D_BACK,    KEY_DOWNARROW },
    { K4D_LEFT,    KEY_LEFTARROW }, { K4D_RIGHT,   KEY_RIGHTARROW },
    { K4D_STRAFEL, KEY_STRAFE_L },  { K4D_STRAFER, KEY_STRAFE_R },
    { K4D_FIRE,    KEY_FIRE },      { K4D_USE,     KEY_USE },
    { K4D_RUN,     KEY_RSHIFT },    { K4D_ESCAPE,  KEY_ESCAPE },
    { K4D_ENTER,   KEY_ENTER },     { K4D_MAP,     KEY_TAB },
    { K4D_WEAPUP,  '[' },           { K4D_WEAPDN,  ']' },
    { K4D_YES,     'y' },           { K4D_NO,      'n' },
};

static void poll_keys(void)
{
    uint32_t now, changed;
    if (!shm) return;
    now = shm->held;
    changed = now ^ held_last;
    if (!changed) return;
    for (unsigned i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
        if (changed & keymap[i].bit)
            push((now & keymap[i].bit) ? 1 : 0, keymap[i].key);
    held_last = now;
}

void DG_Init(void)
{
    const char *name = getenv("K4510_DOOM_SHM");
    int fd;
    if (!name || !*name) {
        fprintf(stderr, "doom: no K4510_DOOM_SHM -- this is the Tube's DOOM and\r\n"
                        "      wants the emulator to start it (type DOOM at the prompt).\r\n");
        return;                        /* the engine still runs; nothing is displayed */
    }
    fd = open(name, O_RDWR);
    if (fd < 0) { fprintf(stderr, "doom: cannot open %s\r\n", name); return; }
    shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED) { shm = NULL; fprintf(stderr, "doom: cannot map the frame buffer\r\n"); return; }
    if (shm->magic != DOOM_MAGIC) { fprintf(stderr, "doom: the frame buffer is not ours\r\n"); munmap(shm, sizeof *shm); shm = NULL; }
}

void DG_DrawFrame(void)
{
    if (!shm) return;
    if (shm->quit) exit(0);            /* the machine took the Tube back */

    /* CMAP256 makes DG_ScreenBuffer one byte a pixel, so the frame is a
     * straight copy -- no colour conversion, and none wanted: VICKY's
     * bitmap is 8 bpp with a palette of its own, which is the same shape
     * DOOM has drawn in since 1993. */
    memcpy(shm->fb, DG_ScreenBuffer, sizeof shm->fb);

    if (palette_changed) {
        for (int i = 0; i < 256; i++) {
            shm->pal[i * 3 + 0] = colors[i].r;
            shm->pal[i * 3 + 1] = colors[i].g;
            shm->pal[i * 3 + 2] = colors[i].b;
        }
        shm->pal_seq++;
        palette_changed = 0;
    }
    __sync_synchronize();              /* the pixels land before the count says so */
    shm->seq++;
    poll_keys();
}

void DG_SleepMs(uint32_t ms)
{
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

uint32_t DG_GetTicksMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    poll_keys();
    if (qw == qr) return 0;
    *pressed = q[qr % QN].pressed;
    *key = q[qr % QN].key;
    qr++;
    return 1;
}

void DG_SetWindowTitle(const char *title) { (void)title; }

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
    return 0;
}
