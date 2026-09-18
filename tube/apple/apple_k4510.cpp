// SPDX-License-Identifier: GPL-2.0-only
/* apple_k4510.cpp -- an Apple IIe on the Tube: LinApple's core, the K4510's
 * frontend.  The seventh co-processor ($D803 = 7).
 *
 * Doc, 2026-09-17: "can you have a look at making an apple IIe emulator ...
 * Tube co-processor ... games should work", and, on why not port one: "tell
 * me why we just dont port an existing apple iie emulator".  No reason; DOOM
 * set the precedent.  This is LinApple (AppleWin's core, brought to Linux and
 * lately rebuilt around a headless-capable core) with this file where its
 * headless Main.cpp would be: it runs the machine a frame at a time and moves
 * the picture, the sound and the keys across the same shared segment DOOM
 * uses (tube/doom/doomgeneric_k4510.c has the identical struct).
 *
 *   the picture   LinApple hands over 560x384 RGBA a frame.  The Apple has
 *                 few colours, so each new one is given a palette entry as it
 *                 appears and the frame crosses as palette indices, which is
 *                 what VICKY's bitmap wants: exact colours, not a cube.
 *   the sound     the speaker (and a Mockingboard, if a program finds one) at
 *                 44100 Hz, mixed to one 8-bit stream for the DigiMAX's DAC 0.
 *   the keys      events from the emulator: an Apple ASCII code with its
 *                 modifiers, the two Apple keys, a joystick from the keypad.
 *
 * The ROMs are the Enhanced IIe's, embedded as LinApple embeds them (and, it
 * turned out, byte for byte what Doc read out of his own machines).  A disk
 * image is named on the command line -- APPLE NAME.DSK at the prompt -- and
 * with none the machine boots to the monitor, as a IIe with an empty drive
 * does not: it boots LinApple's Master.dsk if that is beside the program.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/LinAppleCore.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/Video.h"                     /* g_videotype: the colour rendering (NTSC TV vs the raw artifact striping) */
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/ProgramLoader.h"
#include "apple2/CPU.h"

/* ---- the segment: core/io.c and tube/doom/doomgeneric_k4510.c say the same -- */
#define SHM_MAGIC 0x4E344D44u              /* "DM4N": the fourth shape, with a size and a key ring */
#define FB_MAX_W 560
#define FB_MAX_H 384
#define OPL_RING_N 2048
#define PCM_RING_N 8192
#define KEY_RING_N 256
struct tube_shm {
    uint32_t magic, seq, held, quit, pal_seq, fb_w, fb_h, pad;
    uint8_t  pal[256 * 3];
    uint8_t  fb[FB_MAX_W * FB_MAX_H];
    uint32_t opl_w, opl_r; uint16_t opl_ring[OPL_RING_N];
    uint32_t pcm_w, pcm_r; uint8_t pcm_ring[PCM_RING_N];
    uint32_t key_w, key_r; uint32_t key_ring[KEY_RING_N];
};
/* a key event: bits 0-7 the code, 8-15 flags (1 shift, 2 ctrl, 4 down),
 * 16-23 what it is, 24-31 a value */
enum { KE_KEY = 0, KE_OPEN_APPLE = 1, KE_CLOSED_APPLE = 2, KE_JOY_AXIS = 3, KE_JOY_BUTTON = 4, KE_RESET = 5, KE_ALL_UP = 6, KE_VIDEO = 7, KE_BOOT = 8, KE_PAUSE = 9 };
/* status the control panel reads back, in shm->pad (unused by the picture):
 * byte 0 the video mode (g_videotype), bit 8 paused, bit 9 a disk in drive 1 */
#define ST_PAUSED  0x100u
#define ST_DISK1   0x200u
#define KF_SHIFT 1
#define KF_CTRL  2
#define KF_DOWN  4

static tube_shm *shm;
static AppConfig_t g_config;        /* kept so a cold boot can re-insert the same media */
static bool g_paused;               /* the panel's Pause: the CPU is frozen, keys still taken */

/* A cold boot from the panel: the same dance main() does at start -- a hard
 * reset (which remakes the peripherals), the disk and spindle queued, both
 * worked before the CPU's first instruction.  The disk stays in because
 * load_initial_media re-inserts it from g_config. */
static void cold_boot(void)
{
    linapple_reset_hard();
    peripheral_manager_think(0);
    app_controller_load_initial_media(&g_config);
    peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
    peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
    peripheral_manager_think(0);
    cpu_reset();
    linapple_set_caps_lock_state(true);
    g_paused = false;
}

/* ---- the picture ------------------------------------------------------------- */
static uint32_t pal_rgb[256]; static int pal_n;
static uint8_t  pal_cache_idx[4096]; static uint32_t pal_cache_rgb[4096];   /* the last colour seen at each 12-bit hash */

static uint8_t colour_index(uint32_t rgb)
{
    unsigned h = ((rgb >> 12) ^ (rgb >> 4) ^ rgb) & 4095;
    if (pal_cache_rgb[h] == rgb && pal_cache_idx[h]) return pal_cache_idx[h];
    for (int i = 1; i < pal_n; i++) if (pal_rgb[i] == rgb) { pal_cache_rgb[h] = rgb; pal_cache_idx[h] = (uint8_t) i; return (uint8_t) i; }
    if (pal_n < 256) {                                     /* a new colour: the next entry is its */
        int i = pal_n++;
        pal_rgb[i] = rgb;
        shm->pal[i * 3 + 0] = (uint8_t)(rgb >> 16); shm->pal[i * 3 + 1] = (uint8_t)(rgb >> 8); shm->pal[i * 3 + 2] = (uint8_t) rgb;
        shm->pal_seq++;
        pal_cache_rgb[h] = rgb; pal_cache_idx[h] = (uint8_t) i;
        return (uint8_t) i;
    }
    { int best = 1; long bd = 1L << 40;                    /* 255 in use: the nearest.  A IIe never gets here */
      for (int i = 1; i < 256; i++) { long dr = (long)((pal_rgb[i] >> 16) & 255) - ((rgb >> 16) & 255), dg = (long)((pal_rgb[i] >> 8) & 255) - ((rgb >> 8) & 255), db = (long)(pal_rgb[i] & 255) - (rgb & 255);
                                      long d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = i; } }
      return (uint8_t) best; }
}
static void video_callback(const uint32_t *pixels, int width, int height, int pitch)
{
    if (!shm) return;
    if (width > FB_MAX_W) width = FB_MAX_W;
    if (height > FB_MAX_H) height = FB_MAX_H;
    shm->fb_w = (uint32_t) width; shm->fb_h = (uint32_t) height;
    for (int y = 0; y < height; y++) {
        const uint32_t *src = pixels + (size_t) y * (pitch / 4); uint8_t *dst = shm->fb + (size_t) y * width;
        uint32_t last = 0xFFFFFFFFu; uint8_t li = 0;
        for (int x = 0; x < width; x++) {
            uint32_t rgb = src[x] & 0x00FFFFFFu;
            if (rgb != last) { last = rgb; li = rgb == 0 ? 0 : colour_index(rgb); }   /* black is 0: VICKY's transparent, and the console is off behind it */
            dst[x] = li;
        }
    }
    __sync_synchronize();
    shm->seq++;
    if (shm->quit) exit(0);                               /* the machine took the Tube back */
}

/* ---- the sound --------------------------------------------------------------- */
static void audio_callback(const char *peripheral_id, int slot, const int16_t *const *channels, size_t num_channels, size_t num_samples)
{
    (void) peripheral_id; (void) slot;
    if (!shm || !num_channels || !num_samples) return;
    uint32_t w = shm->pcm_w;
    for (size_t i = 0; i < num_samples; i++) {
        int acc = 0;
        for (size_t c = 0; c < num_channels; c++) acc += channels[c][i];
        acc /= (int) num_channels;
        if (w - shm->pcm_r >= PCM_RING_N) break;          /* the emulator has stopped taking it: drop, never block the machine */
        shm->pcm_ring[w % PCM_RING_N] = (uint8_t)((acc >> 8) + 128);
        w++;
    }
    __sync_synchronize();
    shm->pcm_w = w;
}

/* ---- the keys ---------------------------------------------------------------- */
static uint32_t last_key;                                 /* the code held down, for the release the emulator sends */
static void take_keys(void)
{
    int pressed = 0;
    if (!shm) return;
    while (shm->key_r != shm->key_w) {
        uint32_t e = shm->key_ring[shm->key_r % KEY_RING_N];
        unsigned code = e & 0xFF, flags = (e >> 8) & 0xFF, kind = (e >> 16) & 0xFF, value = e >> 24;
        bool down = (flags & KF_DOWN) != 0;
        /* One keypress a frame.  The Apple's keyboard is a latch the CPU reads
         * when it gets round to it; two presses taken before it has run would
         * leave only the second (the Dell typed CATAOG, 2026-09-17).  The rest
         * of the ring waits for the next frame. */
        if (kind == KE_KEY && down && pressed) break;
        shm->key_r++;
        switch (kind) {
        case KE_KEY: {
            if (down) pressed = 1;
            KeyboardEvent_t ev = { code, (uint8_t) down, (uint8_t)((flags & KF_SHIFT) != 0), (uint8_t)((flags & KF_CTRL) != 0), 0, 0, { 0, 0, 0 } };
            peripheral_command(0, keyboard_cmd_event, &ev, sizeof ev);
            if (down) last_key = code;
            break; }
        case KE_ALL_UP:
            if (last_key) { KeyboardEvent_t ev = { last_key, 0, 0, 0, 0, 0, { 0, 0, 0 } }; peripheral_command(0, keyboard_cmd_event, &ev, sizeof ev); last_key = 0; }
            break;
        case KE_OPEN_APPLE:   linapple_set_apple_key(0, down); linapple_set_joystick_button(0, down); break;
        case KE_CLOSED_APPLE: linapple_set_apple_key(1, down); linapple_set_joystick_button(1, down); break;
        case KE_JOY_AXIS: { JoystickAxisPayload_t p = { 0, (uint8_t) code, (uint8_t) value, 0 }; peripheral_command(0, JOY_CMD_SET_AXIS, &p, sizeof p); break; }
        case KE_JOY_BUTTON:   linapple_set_joystick_button((int) code, down); break;
        case KE_RESET:        if (down) linapple_reset_soft(); break;   /* Ctrl-Reset: the CPU only, as on the machine -- a hard reset would empty the drive */
        case KE_VIDEO:        if (down) { g_videotype = (g_videotype + 1) % VT_NUM_MODES; video_reinitialize(); } break;   /* Ctrl+Alt+V: cycle the colour rendering.  g_videotype only picks which source tiles are built, so rebuild them (create_identity_palette + video_init_buffers) or the change never shows */
        case KE_BOOT:         if (down) cold_boot(); break;                       /* the panel's Boot: a full cold start, disk back in */
        case KE_PAUSE:        if (down) g_paused = !g_paused; break;              /* the panel's Pause: freeze/thaw the CPU */
        }
    }
}

static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t) t.tv_sec * 1000000u + (uint64_t) t.tv_nsec / 1000u; }

int main(int argc, char **argv)
{
    const char *name = getenv("K4510_DOOM_SHM");         /* the same name for every co-processor with a picture: it is the segment's, not the game's */
    AppConfig_t &config = g_config;                      /* file scope: cold_boot re-inserts from it */
    if (name && *name) {
        int fd = open(name, O_RDWR);
        if (fd >= 0) { shm = (tube_shm *) mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); close(fd); }
        if (shm == MAP_FAILED || (shm && shm->magic != SHM_MAGIC)) { fprintf(stderr, "apple: the shared segment is not ours\r\n"); shm = NULL; }
    } else fprintf(stderr, "apple: no K4510_DOOM_SHM -- this is the Tube's Apple IIe and wants the emulator to start it (APPLE at the prompt)\r\n");
    if (shm) { shm->fb_w = FB_MAX_W; shm->fb_h = FB_MAX_H; memset(shm->fb, 0, sizeof shm->fb); pal_n = 1; }
    if (app_args_parse(argc, argv, &config) != 0) return 1;
    config.is_boot = false;                               /* the boot is done below, once the disk is in; is_boot would reset the peripherals with the insert still queued */
    if (app_controller_initialize(&config) != 0) { fprintf(stderr, "apple: LinApple would not start\r\n"); return 1; }
    { const char *v = getenv("K4510_APPLE_VIDEO"); g_videotype = (v && *v) ? (uint32_t) atoi(v) : VT_COLOR_STANDARD;   /* black stays black; TVEMU tinted it (Doc, 2026-09-18).  Ctrl+Alt+V cycles them */ }   /* AppController leaves NTSC at VT_COLOR_STANDARD -- sharp artifact stripes; TVEMU blends the columns as a real TV did, which is how these games looked (Doc, 2026-09-18: "colors are wrong") */
    linapple_set_video_callback(video_callback);
    linapple_set_audio_channel_callback(audio_callback);
    /* peripheral_command only QUEUES a command; the queue is worked in
     * peripheral_manager_think, which runs with each frame.  So the disk that
     * app_controller_load_initial_media inserts arrived one frame late: the
     * boot ROM had already found an empty drive and dropped into BASIC.  This
     * cost an hour.  Reset first (a hard reset remakes the peripherals), then
     * queue the disk and the spindle (disk_cmd_boot, which LinApple's own
     * frontends send with every reset), then think(0) to have both done before
     * the CPU runs its first instruction. */
    linapple_reset_hard();
    peripheral_manager_think(0);
    app_controller_load_initial_media(&config);
    peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
    peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
    peripheral_manager_think(0);
    cpu_reset();                                          /* and the CPU from its reset vector now: the autostart ROM finds slot 6 spinning with a disk in */
    { DiskStatus_t st = {}; size_t n = sizeof st;
      if (peripheral_query(disk_default_slot, disk_query_status, &st, &n) == peripheral_ok && st.drive0_loaded) fprintf(stderr, "apple: drive 1: %s\r\n", st.drive0_full_path);
      else fprintf(stderr, "apple: drive 1 is empty\r\n"); }
    linapple_set_caps_lock_state(true);                   /* a IIe wakes with CAPS LOCK down, and Applesoft wants it so */

    /* 60 frames a second by the wall clock, 17030 cycles each: the machine's
     * speed, whatever the host's.  A late frame is caught up; a lead is slept. */
    { const uint64_t frame_us = 1000000u / 60u; uint64_t next = now_us();
      for (;;) {
          take_keys();
          if (!g_paused) linapple_run_frame(17030);              /* Pause freezes the CPU; keys are still taken so the panel can thaw it */
          if (shm) {                                             /* the panel reads this back: mode, paused, disk present */
              uint32_t st = (uint32_t)(g_videotype & 0xFFu) | (g_paused ? ST_PAUSED : 0u);
              DiskStatus_t ds = {}; size_t dn = sizeof ds;
              if (peripheral_query(disk_default_slot, disk_query_status, &ds, &dn) == peripheral_ok && ds.drive0_loaded) st |= ST_DISK1;
              shm->pad = st;
          }
          if (shm && shm->quit) break;
          next += frame_us;
          { uint64_t now = now_us(); if (next > now) usleep((useconds_t)(next - now)); else if (now - next > 250000u) next = now; } }
    }
    linapple_set_audio_channel_callback(nullptr);
    linapple_set_video_callback(nullptr);
    app_controller_shutdown();
    return 0;
}
