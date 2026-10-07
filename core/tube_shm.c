#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "vicky.h"
#include "opl2.h"
#include "digimax.h"
static char apple_disk_override[96];                  /* the control panel's disk selector: a name for the next APPLE launch, in place of tube_cmd */
static char apple_cur_disk[96];                       /* the disk name the running Apple was launched with (basename), for the panel */
/* ---- DOOM on the Tube ($D803 kind 6) --------------------------------------
 * Doc, 2026-09-16: "go ahead and build the doom tube".
 *
 * Every other co-processor talks through the pty, and DOOM cannot: a 320x200
 * frame is 64 KB, and 35 of them a second through a byte-at-a-time escape
 * parser written for VDU codes is not a design, it is a dare.  So the pixels
 * take their own road -- a shared segment this side creates and the child
 * mmaps (tube/doom/doomgeneric_k4510.c has the identical struct; if you change
 * one, change the other, because no header can be shared between a program
 * built here and one built there).
 *
 * The same segment carries the keys DOWNWARD.  doomgeneric wants presses and
 * RELEASES; a pty delivers keystrokes and never releases, so a player would
 * walk into a wall and stay there.  The host writes the held mask every frame
 * and the child diffs it.  It is $D104 (IO_KBDHELD) widened, by another road,
 * because the child is a separate process and cannot read a register at all. */
#define DOOM_W 320
#define DOOM_H 200
#define DOOM_MAGIC 0x4E344D44u                   /* "DM4N" -- bumped with the OPL ring, the PCM ring, then (2026-09-17, the Apple IIe) a frame size and a key ring */
#define OPL_RING_N 2048
#define PCM_RING_N 8192
#define KEY_RING_N 256
#define FB_MAX_W 560                             /* the Apple IIe's frame; DOOM's 320x200 sits in the same space */
#define FB_MAX_H 384
struct doom_shm {
    uint32_t magic, seq, held, quit, pal_seq, fb_w, fb_h, pad;
    uint8_t  pal[256 * 3];
    uint8_t  fb[FB_MAX_W * FB_MAX_H];
    uint32_t opl_w, opl_r;                       /* DOOM's music, as OPL2 register writes */
    uint16_t opl_ring[OPL_RING_N];               /* (reg << 8) | val */
    uint32_t pcm_w, pcm_r;                       /* DOOM's effects at 11025 Hz, the Apple's speaker at 44100: one stream, unsigned 8-bit */
    uint8_t  pcm_ring[PCM_RING_N];
    uint32_t key_w, key_r;                       /* the Apple's keys, DOWN the segment: events, not a held mask (tube/apple/apple_k4510.cpp says the bits) */
    uint32_t key_ring[KEY_RING_N];
};
static struct doom_shm *doom_map;
/* The console's colours, kept while DOOM wears its own.
 *
 * Doc, 2026-09-17: "Palette did not come back to normal (as was before game)
 * after I exited.  I had to manually reload it."  DOOM writes all 256 entries
 * -- VICKY has one palette and everything shares it -- so the shell came back
 * in Freedoom's colours.  cmd_exec has always wrapped a program in pal_snap /
 * pal_restore (rom/kernal.c); the Tube's DOOM had nothing of the sort.
 *
 * It is done HERE rather than in the ROM on purpose: doom_shm_close() runs on
 * every way out -- a clean quit, *QUIT, a crash, a kill, the emulator being
 * asked to stop -- while the ROM's loop only gets to tidy up when the session
 * ends the way it was supposed to.  A restore that works only when nothing
 * went wrong is the wrong half of the problem to solve. */
static uint8_t doom_pal_save[256 * 3];
static int doom_pal_saved;
/* The console's text layer, switched off while DOOM has the screen.
 *
 * Doc, 2026-09-17: "artefact on top left of screen (black rectangle)".  Colour
 * 0 is TRANSPARENT on VICKY's bitmap, so wherever DOOM draws black the text
 * layer underneath shows through -- and cmd_doom prints everything the
 * co-processor says to that console, so the engine's startup chatter
 * ("I_InitGraphics: DOOM screen size...") sits there for the whole session.
 * Its character cells carry a background colour too, which after DOOM has
 * loaded its own palette is whatever Freedoom made of that index: a black
 * rectangle over the top-left of the picture.
 *
 * demo/book.c has always done this for its pictures -- save layer 0, write 0,
 * "the text layer off", put it back afterwards.  The printing in cmd_doom
 * stays as it is: it is what makes "no game data" readable when the WAD is
 * missing, and with the layer off it can no longer bleed into the game. */
static uint8_t doom_text_layer;
static int doom_text_saved;
static uint8_t doom_bgcol_saved_val;
static int doom_bgcol_saved;
static void doom_pal_snap(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = vicky_palette_rgb(i);
        doom_pal_save[i * 3 + 0] = (uint8_t)(c >> 16);
        doom_pal_save[i * 3 + 1] = (uint8_t)(c >> 8);
        doom_pal_save[i * 3 + 2] = (uint8_t) c;
    }
    doom_pal_saved = 1;
}
static void doom_text_put_back(void)
{
    if (doom_bgcol_saved) { vicky_write(VR_BGCOL, doom_bgcol_saved_val); doom_bgcol_saved = 0; }   /* the console's blue backdrop back */
    if (!doom_text_saved) return;
    doom_text_saved = 0;
    vicky_write(0x10, doom_text_layer);           /* the console comes back */
}
static void doom_pal_put_back(void)
{
    if (!doom_pal_saved) return;
    doom_pal_saved = 0;
    for (int i = 0; i < 256; i++) {
        vicky_write(VR_PALIDX, (uint8_t) i);
        vicky_write(VR_PALR, doom_pal_save[i * 3 + 0]);
        vicky_write(VR_PALG, doom_pal_save[i * 3 + 1]);
        vicky_write(VR_PALB, doom_pal_save[i * 3 + 2]);   /* the B write commits it */
    }
}
static char doom_shm_name[64];
static uint32_t doom_seq_seen, doom_pal_seen;
static int doom_active;

int io_tube_doom(void) { return doom_active && doom_map != NULL; }

/* MELODY let go of, when DOOM goes.
 *
 * A note on an OPL2 sounds until somebody writes its key-off, and DOOM is
 * usually ended by *QUIT or the machine taking the Tube back -- SIGKILL, so
 * the engine's own I_OPL_ShutdownMusic never runs and whatever chord was
 * sounding would drone on under the shell until something else used the
 * chip.  Like the palette, this is done here because here is the one place
 * every way out passes through.  What a clean quit did manage to queue is
 * performed first; then all nine voices are keyed off and the rhythm bits
 * cleared.  The instruments DOOM loaded are left: they are inaudible with no
 * key on, and the next program sets its own.  (Review, 2026-09-17.) */
/* DOOM's effects, a byte at a time: the DigiMAX's stream (core/digimax.h)
 * calls this 11025 times a second from the audio render, which runs on the
 * emulation thread -- the same one that maps and unmaps the segment. */
static int doom_pcm_pull(void)
{
    uint32_t r, w;
    if (!doom_map) return -1;
    r = doom_map->pcm_r; w = doom_map->pcm_w;
    if (r == w) return -1;
    if (w - r > PCM_RING_N) r = w - PCM_RING_N;  /* overrun, or a child gone mad: the newest */
    { int s = doom_map->pcm_ring[r % PCM_RING_N]; doom_map->pcm_r = r + 1; return s; }
}
static void doom_melody_quiet(void)
{
    digimax_stream(NULL, 0);                     /* the effects stop with the game, and DAC 0 returns to silence */
    if (!doom_map) return;
    io_tube_opl_drain();
    for (int ch = 0; ch < 9; ch++) opl2_write_reg((uint8_t)(0xB0 + ch), 0);
    opl2_write_reg(0xBD, 0);
}
void tube_shm_close(void)
{
    doom_melody_quiet();                         /* before the map goes: the drain reads it */
    doom_pal_put_back();                         /* the shell gets its colours back, however DOOM ended */
    doom_text_put_back();                        /* ...and its text layer, the same way */
    if (doom_map) { munmap(doom_map, sizeof *doom_map); doom_map = NULL; }
    if (doom_shm_name[0]) { shm_unlink(doom_shm_name); doom_shm_name[0] = 0; }
    doom_active = 0; doom_seq_seen = doom_pal_seen = 0;
}
static int doom_shm_make(int kind)
{
    int fd;
    snprintf(doom_shm_name, sizeof doom_shm_name, "/k4510-doom-%d", (int) getpid());
    shm_unlink(doom_shm_name);                   /* a previous run that died badly */
    fd = shm_open(doom_shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { doom_shm_name[0] = 0; return 0; }
    if (ftruncate(fd, (off_t) sizeof(struct doom_shm)) != 0) { close(fd); tube_shm_close(); return 0; }
    doom_map = mmap(NULL, sizeof(struct doom_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (doom_map == MAP_FAILED) { doom_map = NULL; tube_shm_close(); return 0; }
    memset(doom_map, 0, sizeof *doom_map);
    doom_map->magic = DOOM_MAGIC;
    doom_seq_seen = doom_pal_seen = 0;
    digimax_stream(doom_pcm_pull, kind == 7 ? 44100 : 11025);   /* DOOM's effects, or the Apple's speaker, into DAC 0 */
    return 1;
}
void io_doom_input(uint32_t held) { if (doom_map) doom_map->held = held; }
/* The Apple IIe's keys: events down the segment (the bits: tube/apple/apple_k4510.cpp).
 * A full ring drops the event; a game that cannot keep up with a keyboard has
 * bigger problems. */
void io_apple_key(uint32_t ev)
{
    if (!doom_map || tube_prog_now != 7) return;
    if (doom_map->key_w - doom_map->key_r >= KEY_RING_N) return;
    doom_map->key_ring[doom_map->key_w % KEY_RING_N] = ev;
    __sync_synchronize();
    doom_map->key_w++;
}

/* ---- what the Apple IIe control panel reads (sdl/panel.c) -------------------- */
/* The frontend leaves status in shm->pad: the video mode in byte 0, paused and
 * disk-present bits above it (tube/apple/apple_k4510.cpp). */
unsigned io_apple_status(void) { return (io_tube_kind() == 7 && doom_map) ? doom_map->pad : 0; }
const char *io_apple_cur_disk(void) { return io_tube_kind() == 7 ? apple_cur_disk : ""; }

/* The shelf: the disk images in /DISK/APPLE, scanned on demand into a static
 * list the panel points at.  Names only; the launch resolves them (tube_start). */
#define APPLE_MAXDISK 24
static char apple_disks[APPLE_MAXDISK][80]; static int apple_ndisks; static int apple_disks_scanned;
static int apple_disk_ext(const char *n)
{
    const char *d = strrchr(n, '.'); if (!d) return 0;
    static const char *const ok[] = { ".hdv", ".dsk", ".do", ".po", ".nib", ".woz", ".2mg", ".bin", NULL };
    for (int i = 0; ok[i]; i++) if (!strcasecmp(d, ok[i])) return 1;
    return 0;
}
static int apple_disk_cmp(const void *a, const void *b) { return strcasecmp((const char *) a, (const char *) b); }
static void apple_scan_disks(void)
{
    apple_ndisks = 0;
    char dir[600]; snprintf(dir, sizeof dir, "%s/DISK/APPLE", fs_root);
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && apple_ndisks < APPLE_MAXDISK)
        if (e->d_name[0] != '.' && apple_disk_ext(e->d_name)) snprintf(apple_disks[apple_ndisks++], 80, "%s", e->d_name);
    closedir(d);
    qsort(apple_disks, apple_ndisks, 80, apple_disk_cmp);
}
int io_apple_disk_count(void)
{
    if (io_tube_kind() != 7) { apple_disks_scanned = 0; return 0; }
    if (!apple_disks_scanned) { apple_scan_disks(); apple_disks_scanned = 1; }   /* once per Apple session: the shelf does not change under it */
    return apple_ndisks;
}
const char *io_apple_disk_name(int i) { return (i >= 0 && i < apple_ndisks) ? apple_disks[i] : ""; }

/* The panel's disk selector: relaunch the Apple with a chosen image, the way
 * the machine's own $D803 write does -- stop, then start kind 7 with the name
 * queued in apple_disk_override.  Called from the SDL thread, as the menu's
 * Stop the Tube is (io_write(IO_TUBE+3, 2)); the CPU is not stepping then. */
void io_apple_load_disk(const char *name)
{
    if (io_tube_kind() != 7 || !name || !name[0]) return;
    snprintf(apple_disk_override, sizeof apple_disk_override, "%s", name);
    io_write(IO_TUBE + 3, 2);                    /* stop the running Apple */
    io_write(IO_TUBE + 3, 7);                    /* and start it again -- tube_start reads the override */
}
/* The machine's own key -- an ASCII character with kbd_mods, or a KEY_* code
 * -- as the Apple's: ASCII is ASCII (an Apple IIe keyboard is one), the
 * arrows are its four (8 21 11 10), Delete is 127.  The frontend sends the
 * releases and the Apple keys separately (io_apple_key). */
void apple_take_key(uint16_t ent)
{
    uint8_t c = (uint8_t) ent; uint32_t code = 0;
    if (ent & KBD_KEY) {
        switch (c) { case KEY_UP: code = 11; break; case KEY_DOWN: code = 10; break; case KEY_LEFT: code = 8; break; case KEY_RIGHT: code = 21; break;
                     case KEY_DEL: code = 127; break; case KEY_HOME: code = 1; break; case KEY_END: code = 5; break; default: return; }   /* the rest have no Apple key */
    } else code = c;
    { uint32_t mods = (kbd_mods & 1 ? 1u : 0u) | (kbd_mods & 2 ? 2u : 0u);
      io_apple_key(code | (mods | 4u) << 8);       /* down, shift and ctrl as held... */
      io_apple_key(code | mods << 8); }            /* ...and up at once: LinApple's keyboard wants every press let go before the next
                                                    * is taken, and the machine's queue carries presses only.  A tap, as the pipe types. */
}
/* shm_open takes "/name"; the child opens a file, and that name lives under
 * /dev/shm on Linux.  One place to say so. */
static const char *doom_shm_path(void)
{
    static char p[96];
    snprintf(p, sizeof p, "/dev/shm%s", doom_shm_name);
    return p;
}

/* The picture, once a host frame.  Deliberately NOT part of tube_pump: that
 * runs only when the pty has traffic and is rate-limited to once per 100 us,
 * and DOOM can go a minute without sending a byte while drawing all the while. */
/* DOOM's music, performed on MELODY.
 *
 * The co-processor's OPL driver (tube/doom/opl_k4510.c) decides which
 * registers to write and when; they arrive here as (reg, val) pairs and go to
 * the chip through opl2_write_reg(), which saves and restores the address
 * latch so this can land between a program's own ADDR and DATA writes.
 *
 * Called from the frontend's SCANLINE hook, not its frame hook: music wants
 * milliseconds, and a frame is sixteen of them.  ~262 drains a frame instead. */
void io_tube_opl_drain(void)
{
    uint32_t r, w;
    if (!io_tube_doom()) return;
    r = doom_map->opl_r; w = doom_map->opl_w;
    if (r == w) return;
    if (w - r > OPL_RING_N) r = w - OPL_RING_N;   /* overrun: keep the newest */
    while (r != w) {
        uint16_t e = doom_map->opl_ring[r % OPL_RING_N];
        opl2_write_reg((uint8_t)(e >> 8), (uint8_t) e);
        r++;
    }
    doom_map->opl_r = r;
}

void io_tube_frame(void)
{
    if (!io_tube_doom()) return;
    if (doom_map->pal_seq != doom_pal_seen) {    /* DOOM's palette becomes VICKY's */
        doom_pal_seen = doom_map->pal_seq;
        for (int i = 0; i < 256; i++) {
            vicky_write(VR_PALIDX, (uint8_t) i);
            vicky_write(VR_PALR, doom_map->pal[i * 3 + 0]);
            vicky_write(VR_PALG, doom_map->pal[i * 3 + 1]);
            vicky_write(VR_PALB, doom_map->pal[i * 3 + 2]);   /* the B write commits the entry */
        }
    }
    if (doom_map->seq == doom_seq_seen) return;  /* nothing new drawn */
    doom_seq_seen = doom_map->seq;
    /* 320x200 doubled sideways to 640x400 and centred in the 640x480 bitmap:
     * 40 blank lines top and bottom.  Doubling here rather than in the engine
     * keeps the shared segment at 64 KB a frame instead of 256 KB. */
    /* 320x200 across the whole 640x480 glass: doubled sideways, and 200 rows
     * stretched over 480 rather than doubled to 400 with black bands.
     *
     * Doc, 2026-09-17: "Is 320x200 a must?  Could it be 320x240?"  The engine
     * renders 320x200 and nothing changes that (i_video.h), but the frame was
     * always meant to be SHOWN at 4:3 -- DOOM's pixels were 1.2 times taller
     * than wide on a 320x200 CRT, which is why the engine carries a
     * SCREENHEIGHT_4_3 of 240 of its own.  Doubling to 640x400 was therefore
     * wrong twice over: it left the bands Doc saw above and below, and it made
     * everything 20% too squat.  480/200 is exactly the 1.2 the game was drawn
     * for.
     *
     * Each source row lands on two or three destination rows; the repeats are
     * a memcpy of the row just written rather than the doubling loop again. */
    { if ((size_t) TULA_H * TULA_W > TULA_ARENA) return;   /* belt and braces, as before */
      uint8_t *dst = k4510_ram + TULA_GFXB;
      int fw = (int) doom_map->fb_w, fh = (int) doom_map->fb_h;
      if (fw <= 0 || fh <= 0 || fw > FB_MAX_W || fh > FB_MAX_H) return;
      if (fw == DOOM_W && fh == DOOM_H) {                    /* DOOM: doubled and stretched, as above */
          int prev = -1;
          for (int y = 0; y < TULA_H; y++, dst += TULA_W) {
              int sy = y * DOOM_H / TULA_H;
              if (sy == prev) { memcpy(dst, dst - TULA_W, TULA_W); continue; }
              { const uint8_t *src = doom_map->fb + (size_t) sy * DOOM_W;
                for (int x = 0; x < DOOM_W; x++) { dst[x * 2] = src[x]; dst[x * 2 + 1] = src[x]; } }
              prev = sy;
          }
      } else {                                               /* the Apple IIe's 560x384, pixel for pixel, centred: its pixels are already the glass's shape */
          int x0 = (TULA_W - fw) / 2, y0 = (TULA_H - fh) / 2;
          if (x0 < 0 || y0 < 0) return;
          for (int y = 0; y < fh; y++) memcpy(dst + (size_t)(y0 + y) * TULA_W + x0, doom_map->fb + (size_t) y * fw, (size_t) fw);
      } }
}
void doom_bitmap_on(void)
{
    doom_pal_snap();                             /* what the console was wearing, to give back after */
    doom_text_layer = vicky_read(0x10); doom_text_saved = 1;
    doom_bgcol_saved_val = vicky_read(VR_BGCOL); doom_bgcol_saved = 1;
    vicky_write(VR_BGCOL, 0);                     /* palette 0 is black: what a transparent bitmap pixel shows.
                                                  * DOOM fills the whole glass so never revealed it; the Apple's
                                                  * 560x384 is centred and its black (index 0, transparent) pixels
                                                  * showed the console's blue BGCOL through -- white-on-blue in the
                                                  * mono modes (Doc, 2026-09-18).  Black background now, both games. */
    vicky_write(0x10, 0);                        /* the text layer off: nothing shows through the black */
    vicky_write(0x21, 0); tula_vw16(0x22, 0); tula_vw16(0x24, 0);   /* palofs, scroll -- as tula_mode */
    tula_vw16(0x26, TULA_W); tula_vw32(0x28, TULA_GFXB);            /* stride, data */
    vicky_write(0x20, 0x19);                                        /* enable | bitmap | 8 bpp */
    memset(k4510_ram + TULA_GFXB, 0, (size_t) TULA_W * TULA_H);     /* the letterbox stays black */
    tula_on = 1;
}
/* Which game DOOM plays.
 *
 * Doc, 2026-09-17, asked for a WADCHOOSER; its choice is one line in
 * DOOM.CFG, "wad = NAME".  The name is looked for among a folder's own
 * entries, whatever its case -- never joined onto a path, so nothing the file
 * says can walk out of the folder.  With no file, or a name that is not
 * there: freedoom1.wad, as it always was, and failing that the first .wad
 * there is, so a machine with only DOOM1.WAD on it still plays.
 *
 * Two folders, /DISK/DOOM first.  The games moved there the same day (the
 * layer is RAM, and 28 MB that is played once a year does not belong in it:
 * fs/DISK/README.TXT); /APPS/DOOM is still looked in, for a machine whose
 * WAD was put there before the move. */
static int doom_wad_in(const char *dir, const char *want, char *out, size_t max)
{
    char first[64] = "", found[64] = ""; DIR *d; struct dirent *e;
    if (!(d = opendir(dir))) return 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l >= sizeof found || strcasecmp(e->d_name + l - 4, ".wad")) continue;
        if (want[0] && !strcasecmp(e->d_name, want)) { snprintf(found, sizeof found, "%s", e->d_name); break; }
        if (!strcasecmp(e->d_name, "freedoom1.wad")) snprintf(found, sizeof found, "%s", e->d_name);
        if (!first[0] || strcasecmp(e->d_name, first) < 0) snprintf(first, sizeof first, "%s", e->d_name);
    }
    closedir(d);
    if (!found[0] && !first[0]) return 0;
    snprintf(out, max, "%s/%s", dir, found[0] ? found : first);
    return found[0] && want[0] && !strcasecmp(found, want) ? 2 : 1;   /* 2: the very one that was asked for */
}
void doom_wad_path(char *out, size_t max)
{
    static const char *const where[2] = { "DISK/DOOM", "APPS/DOOM" };
    char dir[2][600], want[64] = "", line[128], got[2][700]; int r[2]; FILE *f;
    for (int k = 0; k < 2; k++) snprintf(dir[k], sizeof dir[k], "%.511s/%s", fs_root, where[k]);
    for (int k = 0; k < 2 && !want[0]; k++) {
        char cfg[700]; snprintf(cfg, sizeof cfg, "%s/DOOM.CFG", dir[k]); fs_casefix(cfg, sizeof cfg);
        if (!(f = fopen(cfg, "r"))) continue;
        while (fgets(line, sizeof line, f)) {
            char *eq = strchr(line, '='), *v, *z;
            if (line[0] == '#' || !eq) continue;
            for (v = eq + 1; *v == ' ' || *v == '\t'; v++) ;
            for (z = v + strlen(v); z > v && (z[-1] == '\n' || z[-1] == '\r' || z[-1] == ' ' || z[-1] == '\t'); ) *--z = 0;
            if (!strncasecmp(line, "wad", 3)) snprintf(want, sizeof want, "%s", v);
        }
        fclose(f);
    }
    for (int k = 0; k < 2; k++) r[k] = doom_wad_in(dir[k], want, got[k], sizeof got[k]);
    if (r[0] == 2 || (r[0] && r[1] != 2)) snprintf(out, max, "%s", got[0]);        /* the one asked for, wherever it is; else /DISK before /APPS */
    else if (r[1]) snprintf(out, max, "%s", got[1]);
    else snprintf(out, max, "%s/freedoom1.wad", dir[0]);                             /* nothing anywhere: a name for the "no game data" message to fail on */
}
int tube_shm_open(int kind)
{
    if (!doom_shm_make(kind)) return 0;
    doom_active = 1;
    return 1;
}
void tube_shm_quit(void) { if (doom_map) doom_map->quit = 1; }
/* APPLE [disk]: the panel's selector wins over the typed name, and the panel
 * shows the name the machine was launched with. */
void apple_launch_name(char *cmd, size_t n)
{
    if (apple_disk_override[0]) { snprintf(cmd, n, "%s", apple_disk_override); apple_disk_override[0] = 0; }
    { const char *b = strrchr(cmd, '/'); snprintf(apple_cur_disk, sizeof apple_cur_disk, "%s", cmd[0] ? (b ? b + 1 : cmd) : "(DOS 3.3)"); }
}
/* In the Tube's forked child (tube_start), DOOM: returns only if it could not start. */
void doom_child_exec(void)
{
    char *bin = realpath("tube/doom/doomk4510", NULL);   /* before the chdir, as everything here is */
    char wad[900]; doom_wad_path(wad, sizeof wad);
    char *rwad = realpath(wad, NULL);
    setenv("K4510_DOOM_SHM", doom_shm_path(), 1);
    if (chdir(fs_root) != 0) { }
    if (bin && rwad) execl(bin, "doomk4510", "-iwad", rwad, (char *) NULL);
    if (bin && !rwad) {
        const char *m = "doom: no game data.  /APPS/DOOM/WADCHOOSER fetches a game into /DISK/DOOM\r\n"
                        "      (or tools/get-freedoom.sh on the host: Freedoom, 28 MB, BSD licensed).\r\n";
        ssize_t n = write(1, m, strlen(m)); (void) n;
    }
}
/* In the Tube's forked child, the Apple IIe: returns only if it could not start. */
void apple_child_exec(const char *cmd)
{
    char *bin = realpath("tube/apple/apple_k4510", NULL);
    char disk[900]; const char *slot = "--d1"; struct stat asb; disk[0] = 0;
    /* APPLE NAME: a bare name is a disk in /DISK/APPLE (where the images
     * live, as DOOM's WADs are in /DISK/DOOM); a name with a slash is a
     * path from where the shell is.  The bare name was resolved against
     * /HOME and came up missing -- the //e splash hung (Doc, 2026-09-17). */
    if (cmd[0]) {
        char rel[256];
        if (!strchr(cmd, '/') && !strchr(cmd, '\\')) { snprintf(disk, sizeof disk, "%s/DISK/APPLE/%s", fs_root, cmd); fs_casefix(disk, sizeof disk); }
        if ((!disk[0] || stat(disk, &asb)) && !fs_resolve(cmd, rel, sizeof rel, disk, sizeof disk)) fs_casefix(disk, sizeof disk);   /* not in /DISK/APPLE: as a path */
        { char *rp = realpath(disk, NULL); if (rp) { snprintf(disk, sizeof disk, "%s", rp); free(rp); } }   /* ABSOLUTE, before the chdir below: fs_root is relative ("fs"), and a relative disk path was then read against fs_root/fs -- the //e splash hung (Doc, 2026-09-18) */
    }
    if (disk[0]) {                        /* a hard-disk image (a ProDOS volume, .hdv, or any image bigger than a floppy) goes in slot 7, not the floppy in slot 6 */
        struct stat sb; size_t l = strlen(disk);
        if ((l > 4 && !strcasecmp(disk + l - 4, ".hdv")) || (!stat(disk, &sb) && sb.st_size > 900000)) slot = "--hd1";
    }
    if (!disk[0]) { char *m = realpath("tube/apple/linapple/res/Master.dsk", NULL); if (m) snprintf(disk, sizeof disk, "%s", m); free(m); }   /* nothing named: DOS 3.3's master, so a IIe with an empty drive does not sit there spinning */
    setenv("K4510_DOOM_SHM", doom_shm_path(), 1);
    setenv("HOME", "/tmp", 1);            /* LinApple keeps a registry under $HOME/.local/share; the machine's home is not the place for it */
    if (chdir(fs_root) != 0) { }
    if (bin) execl(bin, "apple_k4510", slot, disk, (char *) NULL);
    { const char *m = "apple: tube/apple/apple_k4510 is not built (make -C tube/apple -f Makefile.k4510)\r\n"; ssize_t n = write(1, m, strlen(m)); (void) n; }
}
