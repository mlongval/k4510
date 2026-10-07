/* The I/O page's devices, as they reach each other.
 *
 * Until 2026-10-07 every device behind $D000-$DFFF lived in core/io.c, 2,800
 * lines that had grown a section at a time.  It is now one file a device, and
 * io.c keeps what is truly the page's: the keyboard and mouse, WAIT, DMA, the
 * dispatch by page, reset and the save state.
 *
 *   io.c        the dispatch, keyboard, mouse, WAIT, DMA, reset, save state
 *   sys.c       SYS $D500: clock, frames, version, switches, the title
 *   seq.c       the sound sequencer ($D5E0-$D5E3)
 *   fred.c      FRED, the MATH unit ($D700)
 *   hostfs.c    the file device ($D300): the sandbox, mounts, URLs
 *   status.c    STATUS and MOUNT's report: what the host knows of itself
 *   tube.c      the Tube ($D800): the co-processors on a pty, the Tube ULA
 *   tube_shm.c  DOOM and the Apple IIe: the Tube's shared frame buffer
 *   screen2.c   the second screen's session (Alt+2)
 *   debug.c     WATCH, the recorder, DUMP, IDEA's brainshots
 *
 * io.h is the machine's face to the frontend; this header is internal to
 * those files and nothing outside core/ includes it. */
#ifndef K4510_IO_INT_H
#define K4510_IO_INT_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* io.c: the keyboard, the mouse, WAIT, DMA */
#define KBD_KEY 0x100              /* a queue entry's kind: a KEY_* code, not a character */
extern uint8_t kbd_mods;           /* Shift 1, Ctrl 2, Alt 4, as held now */
void io_wait_start(void);          /* WAIT: asleep until an interrupt or a key */
void mouse_host_release(void);     /* a program's $D110 wish for the host pointer ends with it */
const uint8_t *io_dma_regs(void);  /* for the dump */

/* sys.c */
extern uint32_t sys_frames;
extern const char sys_version[16];
extern unsigned sys_cpu_khz;
uint8_t sys_read(uint8_t r);
void    sys_write(uint8_t r, uint8_t v);
void    sys_reset(void);
void    sys_state_save(FILE *f);
int     sys_state_load(FILE *f);
void    title_file(const char *host_path);   /* the file device opened, loaded or saved this */
int     title_depth_now(void);

/* seq.c */
void seq_write(uint8_t r, uint8_t v);
void seq_tick(void);
void seq_reset(void);
void seq_state_save(FILE *f);
int  seq_state_load(FILE *f);

/* fred.c */
uint8_t fred_read(uint8_t r);
void    fred_write(uint8_t r, uint8_t v);
void    fred_reset(void);
void    fred_state_save(FILE *f);
int     fred_state_load(FILE *f);
void    fred_dump(FILE *f);

/* hostfs.c */
extern char fs_root[512];
extern char fs_cwd[256];           /* relative to fs_root, no leading/trailing slash; "" = root */
uint8_t hostfs_read(uint8_t r);
void    hostfs_write(uint8_t r, uint8_t v);
void    hostfs_reset(void);
void    hostfs_state_save(FILE *f);
int     hostfs_state_load(FILE *f);
void    hostfs_after_load(void);   /* the open file and the mounts do not survive a load */
const uint8_t *hostfs_regs(size_t *n);
int     fs_resolve(const char *name, char *rel, size_t relmax, char *out, size_t outmax);
void    fs_casefix(char *path, size_t max);
int     fs_guest_str(uint32_t p, char *name, size_t max);
int     fs_mount_count(void);
const char *fs_mount_at(int i);
const char *fs_mount_src(int i);

/* status.c */
const char *status_row(int idx);                    /* STATUS: line idx; 0 takes the snapshot */
int     status_mount_row(int idx, char *b, size_t max);   /* MOUNT alone: where the machine's disk is */

/* tube.c */
extern int tube_prog_now;          /* the Tube program started last, while it lives */
extern int tube_prog_at;           /* the title depth it started at */
extern int io_lock_linux;
uint8_t tube_status(void);
uint8_t tube_io_read(uint8_t r);
void    tube_io_write(uint8_t r, uint8_t v);
void    tube_reset(void);
void    tube_stop(void);
void    tube_state_save(FILE *f);
int     tube_state_load(FILE *f);
void    tube_log(const char *fmt, ...);
/* the Tube ULA's bitmap, which DOOM and the Apple draw into too */
#define TULA_GFXB 0x200000u
#define TULA_W 640
#define TULA_H 480
#define TULA_SPRTAB 0x260000u
#define TULA_ARENA (TULA_SPRTAB - TULA_GFXB)
extern uint8_t tula_on;
void tula_vw16(uint8_t r, int v);
void tula_vw32(uint8_t r, uint32_t v);

/* tube_shm.c */
int  tube_shm_open(int kind);      /* DOOM (6) or the Apple (7): the segment, before the fork; 0 on failure */
void tube_shm_close(void);
void tube_shm_quit(void);          /* tell a live child to go */
void doom_bitmap_on(void);
void doom_wad_path(char *out, size_t max);
void doom_child_exec(void);        /* in the forked child: exec DOOM, or say why not */
void apple_launch_name(char *cmd, size_t n);   /* the panel's disk, if one was chosen; the name the panel shows */
void apple_child_exec(const char *cmd);
void apple_take_key(uint16_t ent);

/* screen2.c */
void s2_key(uint16_t ent);
void s2_pump(void);

/* debug.c */
void    screen_geom(int *cols, int *rows);
void    dbg_key(uint8_t k);
int     dbg_reg_read(uint8_t r, uint8_t *v);   /* SYS's debug registers: 1 if r is one */
void    dbg_reg_write(uint8_t r, uint8_t v);
void    dbg_frame(void);
void    dbg_reset(void);
uint8_t idea_next(void);
void    idea_add(uint8_t c);
void    idea_write(uint8_t how);

#endif
