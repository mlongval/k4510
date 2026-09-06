/* K4510 I/O page: $D000-$DFFF in the unmapped CPU view.
 *
 * This layout is the agreed K4510 I/O map (design doc, Plan §3,
 * 2026-08-22). Every device is reached through its base constant.
 */
#ifndef K4510_IO_H
#define K4510_IO_H
#include <stdint.h>

#define IO_BASE        0xD000u
#define IO_VICKY       0xD000u   /* $D000-$D0FF  (Phase 4a)              */
#define IO_INPUT       0xD100u   /* $D100-$D1FF  keyboard, joysticks     */
#define IO_DMA         0xD200u   /* $D200-$D2FF  block DMA (C-18)        */
#define IO_STORAGE     0xD300u   /* $D300-$D3FF  host filesystem (D-09)  */
#define IO_SOUND       0xD400u   /* $D400-$D4FF  the sound page: $D400-$D47F is empty (the SIDs, until 2026-09-05) */
#define IO_FM          0xD480u   /* $D480-$D4FF  OPL2 (YM3812), DigiMAX  */
/* The OPL2, wired the AdLib's way, so every AdLib register list means what it
 * says here.  The machine's one sound chip.
 *   $D480  W ADDR    the register to write next
 *          R STATUS  bit7 IRQ, bit6 timer 1 expired, bit5 timer 2 expired
 *   $D481  W DATA    write it;  R  the last value written to that register
 *   $D482  R ID      $02 = an OPL2 is fitted */
#define IO_SYS         0xD500u   /* $D500-$D5FF  system: clock, RTC, version  */
#define IO_SYS_HOST    0xD522u   /* R: 0 = desktop, 1 = Raspberry Pi */
#define IO_SYS_OPTS    0xD521u   /* R: what the host's menu has switched on, for the ROM */
#define SYSOPT_CPMCOM  0x01     /*    an unknown word may run a CP/M .COM */
#define SYSOPT_NOBOOT  0x04     /*    do NOT run /STARTUP.BAT: the way out of one that wedges the machine */
#define SYSOPT_MARGIN  0x02     /*    the one-cell top/left margin wanted with it (79 columns, not 80) */
#define SYSOPT_STATUS  0x08     /*    the status-bar text mode: the console is a scroll region between two static bands */
#define SYSOPT_MODEREQ 0x10     /*    a mode CHANGE is asked for (the host holds it up for some frames) */
#define SYSOPT_MODE    0xE0     /*    bits 5-7: the video mode the host wants, ALWAYS published as
                                 *    mode + 1 (0 = an old host that does not publish).  At power-on the
                                 *    ROM boots straight into it; with SYSOPT_MODEREQ up it performs the
                                 *    change the next time it reads a key -- the frontend cannot do it
                                 *    alone, the console's PCOLS/PROWS/stride are the ROM's.  The guest
                                 *    acknowledges by writing this register; the host also gives up (and
                                 *    puts its menu setting back) if VICKY's CTRL never changes. */
#define SYSOPT_MODE_SHIFT 5
void    io_set_opts(uint8_t v);
/* The status bands and the clock's format, for the guest at $D52D-$D52F.
 * $D521 was the natural home and is FULL: all eight bits are spoken for
 * (CP/M-by-name, margin, STARTUP.BAT, the bands, the mode request, and three
 * of the video mode).  So these are their own bytes, and there is room after
 * them for whatever the menu grows next. */
void    io_set_bands(uint8_t top, uint8_t bot, uint8_t clockfmt);
int     io_mode_acked(void);      /* 1 once: the guest performed the video-mode request */
void    io_set_clock_measured(int yes);  /* the frontend: has this host a measured clock in k4510.cfg? */
int     io_clock_measured(void);         /* ...and back again */
int     io_adopt_requested(void);        /* 1 once: the guest asked to keep the clock in force (SETUP) */
int     io_measuring(void);              /* the guest is measuring: the governor must keep its hands off */
extern uint16_t io_audio_fill;           /* samples the sound made WITHOUT the machine, because it was late */
#define IO_BANK        0xD600u   /* $D600-$D6FF  bank registers (K-01)   */
#define IO_NET         0xD900u   /* $D900-$D9FF  the N: device: TCP and HTTP channels (core/net.h) */
/*      IO_TERM        0xDA00     $DA00-$DAFF  JIM, the terminal: a VT100/ANSI in hardware (core/term.h) */
#define IO_TUBE        0xD800u   /* $D800-$D8FF  the Tube: a co-processor running BBC BASIC (or CP/M, desktop only) */
#define IO_MATH        0xD700u   /* $D700-$D7FF  math unit: float registers + MEGA65-style mul/div */
#define IO_FAR         0xDF00u   /* $DF00-$DFFF  far-call gate (K-02)    */
/* BANK registers: $D600 + 4n, n = 0..7, one per 8 KB block of the CPU view.
 *   bytes 0-2  physical base bits 0-23 (little-endian); they only set the base
 *   byte 3     bits 24-27 of the base, and bit 7 = OFF. Writing byte 3 switches
 *              the block: bit 7 clear = on, set = off. (So STQ works, and a
 *              byte-wise save/restore never switches a block on by accident.)
 *              Reads give the base; byte 3 reads $FF while the block is off.
 *   $D620  read: bit n = block n banked     $D621  read: MAP mask (bit n = MAPped)
 * A banked block resolves phys = base + (cpu & $1FFF). MAP rewrites all
 * eight blocks, so the ROM's "MAP off" at program exit clears the banks too.
 * The I/O page $D000-$DFFF and the stub page $FF00-$FFFF stay visible whatever
 * is banked, so banking blocks 6 and 7 reveals the RAM under the ROM only.
 * FAR gate: JSR $DF00 + 4n calls descriptor n of the table at FARTAB.
 *   $DF00-$DF7F  32 call slots      $DFF0  return gate (RTS lands here)
 *   $DF80-$DF83  FARTAB 28-bit pointer to the descriptor table (read/write)
 *   $DF84  read: nesting depth      $DF85  read: last error (1 overflow, 2 underflow, 3 bad slot); write clears
 *   descriptor, 8 bytes: base[4] block flags entry[2]; flags bit0 leave
 *   banked on return, bit1 do not bank (long jump to resident code).
 *   A/X/Y/Z pass through both ways: cc65 __fastcall__ works across it. */
/* MATH unit. Results are ready the cycle after the write that triggers them.
 *   $D700-$D71F  F0..F7   eight IEEE-754 single registers, little-endian, read/write
 *   $D720  FOP     write = execute; low 5 bits op, see below
 *   $D721  FARG    (dst << 4) | src, register numbers 0..7, write before FOP
 *   $D722  FFLAGS  from the last op: bit0 result zero, bit1 negative, bit2 NaN/inf
 *   $D724-$D727  FI  int32 for ITOF / FTOI
 *   ops: 0 MOV Fd=Fs  1 ADD  2 SUB  3 MUL  4 DIV   (Fd = Fd op Fs)
 *        5 SQRT 6 SIN 7 COS 8 TAN 9 ATAN 10 EXP 11 LOG 14 ABS 15 NEG 16 FLOOR 17 ROUND  (Fd = f(Fs))
 *        10 ATAN2 (Fd = atan2(Fd,Fs))  13 POW (Fd = Fd^Fs)  21 FMOD (Fd = fmod(Fd,Fs))
 *        18 CMP   flags from Fd - Fs, registers unchanged
 *        19 ITOF  Fd = (float)FI        20 FTOI  FI = (int32)Fs, truncated
 *   Math list -- a program for the unit in RAM, run with one write:
 *   $D728-$D72B MLPTR   28-bit pointer to the list
 *   $D72C  MLRUN   write = run from MLPTR until END or a STOP fires
 *   $D72D  MLSTAT  0 reached END, 1 a STOP fired, $FF runaway (65536 ops)
 *   $D72E,$D72F MLCNT  16-bit counter for DJNZ, read/write
 *   list ops, 2 bytes each (op, arg) unless noted; ops 0..21 are the FOP ops
 *   with arg = (dst<<4)|src, and in addition:
 *   $80 END            $81 STOPNEG  stop if last flags negative    $82 STOPPOS  if not negative
 *   $83 STOPZERO       $84 STOPNZ           $85 JUMP arg (signed, in ops from the next op)
 *   $86 DJNZ arg       MLCNT--, jump if nonzero
 *   $87 STOPFIGE arg   stop if FI >= arg (unsigned byte compare on the low byte, FI clamped)
 *   $88 LDF arg=dst<<4, then 4 bytes: IEEE single immediate into Fdst (6-byte op)
 *   $89 LDI, then 4 bytes: int32 immediate into FI (6-byte op)
 *   $8A LDMS arg=dst<<4, then 4 bytes: 28-bit address of a Microsoft-format float
 *       (exponent excess-128, mantissa1 with sign in bit 7, mantissa2, mantissa3 --
 *       EhBASIC's packed variable format); converted into Fdst (6-byte op)
 *   MEGA65-compatible integer unit (same addresses as the MEGA65):
 *   $D770-$D773 MULTINA  $D774-$D777 MULTINB  (unsigned 32-bit, LE)
 *   $D778-$D77F MULTOUT  = A * B, 64-bit
 *   $D76C-$D76F DIVOUT integer part of A / B   $D768-$D76B fractional part (32.32)
 *   recomputed on every write to an input byte; B = 0 gives all-ones. */
#define MATH_MOV 0
#define MATH_ADD 1
#define MATH_SUB 2
#define MATH_MUL 3
#define MATH_DIV 4
#define MATH_SQRT 5
#define MATH_SIN 6
#define MATH_COS 7
#define MATH_TAN 8
#define MATH_ATAN 9
#define MATH_ATAN2 10
#define MATH_EXP 11
#define MATH_LOG 12
#define MATH_POW 13
#define MATH_ABS 14
#define MATH_NEG 15
#define MATH_FLOOR 16
#define MATH_ROUND 17
#define MATH_CMP 18
#define MATH_ITOF 19
#define MATH_FTOI 20
#define MATH_FTOA 22         /* F[src] -> MS-BASIC-format ASCII at phys FSPTR ($D730), leading ' '/'-' */
#define MATH_FTOAR 23        /* same, no leading character for positives */
#define MATH_FMOD 21
#define ML_END 0x80
#define ML_STOPNEG 0x81
#define ML_STOPPOS 0x82
#define ML_STOPZERO 0x83
#define ML_STOPNZ 0x84
#define ML_JUMP 0x85
#define ML_DJNZ 0x86
#define ML_STOPFIGE 0x87
#define ML_LDF 0x88
#define ML_LDI 0x89
#define ML_LDMS 0x8A
/* SYS registers (read-only unless noted):
 *   $00,01  CPU clock, kHz, LE (40500)      $02,03  physical RAM, MB, LE (256)
 *   $04     read: latch the host clock into $05-$0C and return 0
 *   $05 sec $06 min $07 hour $08 day $09 month $0A,0B year LE $0C weekday (0=Sun)
 *   $0D,0E,0F  frames since reset, 24-bit LE (vblank count)
 *   $10-$1F  version string, NUL-terminated
 *   $20     ROM base page (e.g. $A0 for a 24 KB ROM)
 *   $F0     write: DUMP -- the host writes dumps/dump-NNN.txt (machine state, screen,
 *           PC history, keys, the shell log); read: the number of the last dump
 *   $F1     write: append a byte to the shell log (the ROM logs command lines and DUMP notes)
 *   $F2     write 1/0: automatic dump every 900 frames (15 s) on/off (on at reset on the desktop,
 *           off on the Pi; DUMP or DUMP ON also arms the per-instruction PC recorder); read: the setting
 *   $23     read: the CPU clock setting in force (0 = 40.5 MHz, 1 = 30, 2 = 20, 3 = 15, 4 = 10);
 *           write: ask for one -- the host applies it next frame (BENCH sweeps them)
 *   $24,25  audio gaps LE: callbacks that found the ring empty since last cleared; any write clears
 */

/* --- input ($D100) ------------------------------------------------------ */
/* Keyboard: a FIFO of key-down events. Printable keys arrive as ASCII
 * ($20-$7E, already shifted/dead-keyed by the host layout on the desktop);
 * control keys as ASCII controls; everything else as $80+ codes. */
#define IO_KBD         (IO_INPUT + 0x00)  /* read: next event, pops; 0 if none.  write: push a key into the queue (type-ahead: a program types at the shell) */
#define IO_KBDST       (IO_INPUT + 0x01)  /* bit7 event available; bit0 shift, bit1 ctrl, bit2 alt held */
#define IO_KBDPEEK     (IO_INPUT + 0x02)  /* read: the next event without popping it; 0 if none */
#define IO_KBDBREAK    (IO_INPUT + 0x03)  /* read: an ESC ($1B) or Ctrl-C ($03) waiting anywhere in the queue is removed and returned; 0 if none */
/* The keys HELD right now, for games -- the queue above is events, and a
 * machine that only reports presses cannot tell a game when to stop moving
 * (LODE and BOMBER were unplayable for exactly that, 2026-09-05).  A joystick,
 * in effect: the host refreshes it every frame from its own key state (SDL's
 * on the desktop, the C64 matrix on the Pi).  Read-only, live, no queue. */
#define IO_KBDHELD     (IO_INPUT + 0x04)  /* read: HELD_* bits for the keys down at this moment */
#define HELD_UP    0x01
#define HELD_DOWN  0x02
#define HELD_LEFT  0x04
#define HELD_RIGHT 0x08
#define HELD_FIRE  0x10                   /* space */
#define HELD_A     0x20                   /* Z */
#define HELD_B     0x40                   /* X */
void    kbd_held(uint8_t mask);           /* the host, once a frame: which of those are down */
#define KEY_ENTER 0x0D
#define KEY_BS    0x08
#define KEY_TAB   0x09
#define KEY_ESC   0x1B
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_PGUP  0x86
#define KEY_PGDN  0x87
#define KEY_INS   0x88
#define KEY_DEL   0x89
#define KEY_F1    0x90                    /* F1..F12 = $90..$9B */

/* --- storage ($D300): the host filesystem, sandboxed to one directory --- */
/* Names are NUL-terminated, at NAMEPTR. Transfers go straight to RAM. */
#define IO_FS_CMD      (IO_STORAGE + 0x00) /* write: command; read: 0 idle */
#define IO_FS_STATUS   (IO_STORAGE + 0x01) /* 0 ok, 1 not found, 2 io error / not a dir / not empty, 3 bad cmd, 4 end of dir, 5 name too long */
#define IO_FS_NAMEPTR  (IO_STORAGE + 0x04) /* 28-bit */
#define IO_FS_ADDR     (IO_STORAGE + 0x08) /* 28-bit RAM address for READ/WRITE/DIRNEXT */
#define IO_FS_LEN      (IO_STORAGE + 0x0C) /* 32-bit: bytes requested; updated to bytes done */
#define IO_FS_SIZE     (IO_STORAGE + 0x10) /* 32-bit: file size after OPEN/STAT */
#define FS_OPEN_READ   1   /* open NAMEPTR for reading; SIZE = file size; offset = 0 */
#define FS_OPEN_WRITE  2   /* create/truncate NAMEPTR for writing */
#define FS_READ        3   /* read LEN bytes at the current offset into ADDR; LEN = bytes read */
#define FS_WRITE       4   /* write LEN bytes from ADDR */
#define FS_CLOSE       5
#define FS_DIR_FIRST   6   /* start a directory listing */
#define FS_DIR_NEXT    7   /* copy next entry name (NUL-terminated) to ADDR, SIZE = its size; status 4 at end */
#define FS_STAT        8   /* SIZE = size of NAMEPTR, status 1 if absent */
#define FS_LOAD        9   /* convenience: OPEN_READ + read whole file to ADDR + CLOSE; LEN = size */
#define FS_SAVE       10   /* convenience: OPEN_WRITE + write LEN bytes from ADDR + CLOSE */
#define FS_CHDIR      11   /* change the current directory to NAMEPTR (status 1 if absent) */
#define FS_MKDIR      12   /* create directory NAMEPTR */
#define FS_RM         13   /* delete file NAMEPTR (1 absent, 2 is a directory / failed) */
#define FS_RMDIR      14   /* delete directory NAMEPTR (must be empty) */
#define FS_GETCWD     15   /* write the current directory ("/..." NUL-terminated) to ADDR; SIZE = length */
#define FS_RENAME     16   /* rename NAMEPTR to the name string ADDR points at */
#define FS_COPYFILE   17   /* copy file NAMEPTR to the name string ADDR points at */
#define FS_DIR_ALL    18   /* DIR_FIRST, dotfiles included (only . and .. stay hidden) */
/* Names may contain "/" (and "\"): "/" is the sandbox root, "." and ".."
 * work, ".." never leaves the root. Lookups are case-insensitive when the
 * exact name is absent. Reads (OPEN_READ, STAT, LOAD) of a bare name not
 * found in the current directory also try /PRG and /BASIC. DIR_NEXT lists
 * sorted, directories with SIZE = $FFFFFFFF. */

void    fs_set_root(const char *dir);
const char *fs_get_root(void);
const char *fs_get_cwd(void);

/* --- DMA ($D200) -------------------------------------------------------- */
/* All addresses physical, 28-bit, little-endian. Transfers are instant (§0.5). */
#define IO_DMA_SRC     (IO_DMA + 0x00)    /* 4 bytes */
#define IO_DMA_DST     (IO_DMA + 0x04)    /* 4 bytes */
#define IO_DMA_LEN     (IO_DMA + 0x08)    /* 4 bytes, bytes to move */
#define IO_DMA_CMD     (IO_DMA + 0x0C)    /* write: 1 copy, 2 fill (value = SRC byte 0), 3 swap; read: 0 = idle */
#define IO_DMA_STATUS  (IO_DMA + 0x0D)    /* read: last command, or $FF if bad */

/* --- boot-time data the frontend places in RAM (until the system ROM carries it) --- */
#define K4510_FONT8_PHYS   0x00010000u   /* 256 glyphs x 8 rows, ASCII order, 2 KB at 64 KB */
#define K4510_SCREEN_PHYS  0x00000800u   /* text map the ROM uses: 80x60 bytes */

uint8_t io_read(uint16_t addr);
void    io_frame_tick(void);
void    io_set_cpu_khz(unsigned khz);
/* SYS+$36..$39: a free-running millisecond counter, read from this at the
 * moment the guest asks.  For anything that must keep real time rather than
 * frame time -- see the note in io.c. */
void    io_set_ms_source(uint32_t (*fn)(void));
extern uint16_t io_audio_gaps;                  /* counted by the frontend's audio callback */            /* what SYS $00/01 report */
/* --- WATCH ($D530-$D535): who is trampling this byte? ------------------
 *   $30-$33  28-bit physical address to watch
 *   $34      write 1 to arm; write 0 to disarm; reads back the armed state
 *   $35      hits since last arm (saturates at 255)
 * A CPU write to the watched byte -- normal, MAPped, banked or a flat
 * 32-bit store -- fires a DUMP (dumps/dump-NNN.txt, tagged "watch") and
 * disarms, so one bug produces one dump.  DMA and the host itself do not
 * trip it: the question WATCH answers is "which instruction wrote this",
 * and the dump's PC history is the answer. */
#define IO_WATCH_ADDR  (IO_SYS + 0x30)
#define IO_WATCH_CTL   (IO_SYS + 0x34)
#define IO_WATCH_HITS  (IO_SYS + 0x35)
extern uint32_t dbg_watch_addr;
extern uint8_t  dbg_watch_ctl, dbg_watch_hits;
void dbg_watch_hit(void);                    /* mem.c reports; io.c dumps and disarms */

extern uint32_t io_prof_reads, io_prof_writes, io_prof_hist[256];   /* the I/O profile, for PERF.TXT */
extern int      io_prof_on;                  /* frontend: profile only while the PERF window is open */
extern uint64_t io_prof_cycles;
void    io_prof_reset(void);                 /* called by VICKY at vblank */
void    io_write(uint16_t addr, uint8_t v);
void    io_reset(void);

/* The Tube ($D800): Acorn's answer, refitted. The HOST runs Richard
 * Russell's BBC BASIC interpreter (the vendored BBCTTY console edition,
 * tube/bbcbasic) on a pty; the machine talks to it byte-wise:
 *   $D800 R: status  bit0 alive, bit7 a byte waits in $D801
 *   $D801 R: next byte from the co-processor (pops)
 *   $D802 W: a byte to the co-processor (its keyboard)
 *   $D803 W: 1 start (spawn), 2 stop (kill)
 * The co-processor has its own flat 256 MB; PAGE/HIMEM live there, far
 * beyond the 64 KB view. On the Pi the co-processor is the same
 * interpreter (or RunCPM's Z80, program 3) running on core 3
 * (core/tube_cp.c); an unfitted program leaves status reading 0. The
 * console it talks to is JIM, the terminal at $DA00 (core/term.h). */
void    kbd_push(uint8_t code);
void    dbg_pc(uint16_t pc);                 /* mem.c calls this on every opcode fetch */
int     dbg_dump(const char *why);           /* write a dump; returns its number, -1 on failure */
void    kbd_modifiers(uint8_t shift, uint8_t ctrl, uint8_t alt);

#endif
