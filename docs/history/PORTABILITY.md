# Portability review before the Pi build

*2026-08-22. Read against k4510 at commit 06b437d plus the SID fix
below. Scope: `core/` (6,869 lines incl. cpu65.c and reSID). The
frontends (`sdl/`, `test/`) are host code by definition.*

## Frame-time measurement (t480i5, i5-8350U, headless `test/bench`)

Per emulated frame, host time, 600-frame average:

| what runs          | CPU    | VICKY  | reSID   | palette | total   |
|--------------------|--------|--------|---------|---------|---------|
| idle shell, before | 1.94   | 0.53   | **21.54** | 0.37  | 24.37 ms |
| idle shell, after  | 1.89   | 0.50   | 0.63    | 0.36    | 3.38 ms |
| balls.prg          | 1.89   | 0.72   | 0.63    | 0.37    | 3.61 ms |
| cube.prg           | 2.28   | 0.82   | 0.65    | 0.37    | 4.12 ms |

Budget is 16.67 ms. **Before the fix the desktop was 45 % over budget
and nobody could tell** — SDL just ran behind real time. The cause: the
four reSIDs were clocked at the CPU's 40.5 MHz, 41× a real SID. They
now run at 1 MHz whatever the CPU clock (`core/sid.cc`, `SID_HZ`), which
also restores the C64 meaning of the frequency registers.

Extrapolation to a Pi 3B+ (Cortex-A53 @ 1.4 GHz, in-order, ~5–8× slower
than this i5 single-threaded): 17–33 ms per frame if everything stays on
one core. The Pi has four cores. The split falls out naturally —
CPU emulation on one core, VICKY + reSID on another — and the per-line
interface already exists, so **the plan is feasible but not free**; the
first Pi measurement decides whether we need the second core or not.
"palette" is desktop-only (the Pi writes 8-bit indexed directly).

## Host dependencies found

Everything below is in two files. cpu65.c, vicky.c, sid.cc and reSID are
clean C/C++ with no OS calls.

| where | call | Pi/Circle answer |
|---|---|---|
| `mem.c` | `mmap(MAP_NORESERVE)`, `madvise` | Plain allocation of 256 MB from Circle's heap (1 GB on the 3B+; `kernel.cpp` sets the heap size). No lazy commit, so boot costs a 256 MB clear — ~0.1 s. |
| `mem.c` | `fopen/fread` in `mem_load_rom` | Frontend responsibility; Circle's `FATFS`. Move out of core. |
| `io.c` storage `$D300` | `fopen/fgetc/fputc/fclose/fseek/ftell`, `opendir/readdir/closedir`, `stat`, `snprintf` | The whole host side of the storage device becomes `host_fs_*` — Circle's `FATFS` (fatfs) has open/read/write/seek/opendir/readdir/stat. Flat names already (sandbox strips `/`), which matches FAT 8.3-plus-LFN fine. |
| `io.c` SYS `$D500` | `time()`, `localtime()` | Circle: `CTime`/`CTimer` with RTC from NTP if networked, else a counter from boot. Returns 1970 if no clock — acceptable. |
| shim | `DEBUGPRINT` → `fprintf(stderr)` | Circle has `CLogger`. One macro. |
| `sid.cc` | `double` arithmetic | Fine on AArch64. |
| reSID | `new[]` in `filter.cc`, `sid.cc` | Needs `operator new` — Circle provides it (`circle/new.h`). No exceptions, no RTTI, no iostream. Build with `-fno-exceptions -fno-rtti` as we already do. |

## Things checked and found fine

- **Alignment:** all multi-byte reads from emulated RAM are byte-wise
  (`rd32`/`rd16`, `fs_rd32`); no `*(uint32_t *)` punning anywhere.
  Matters on ARM.
- **Endianness:** none assumed; everything is assembled from bytes.
- **Pointer size:** `k4510_ram` is indexed with `uint32_t`; no pointer
  stored in emulated memory.
- **Stack use:** the largest stack objects are `capture`'s, not the
  core's. `vicky.c` keeps its 640-byte line scratch in `static`.
  `sid_render` has a 32 KB `static short tmp[4][4096]`. Circle's default
  kernel stack is 128 KB; fine.
- **64-bit assumptions:** none. `long` appears once (`ftell`).
- **cpu65.c:** uses `__builtin_expect` and `__attribute__` via the shim;
  GCC on the Pi understands both.

## What to do about it (the seam)

Add `core/host.h` — the eight calls the core needs from whoever hosts it:

    void    *host_alloc(size_t bytes);              /* zeroed */
    void     host_log(const char *fmt, ...);
    int      host_clock(host_time *t);              /* y m d h m s wday, 0 if no clock */
    int      host_fs_open(const char *name, int write);
    int      host_fs_read/write/close/seek(...);
    int      host_fs_dir_first/next(char *name, uint32_t *size);
    int      host_fs_stat(const char *name, uint32_t *size);

`sdl/host_posix.c` implements it with what `io.c` and `mem.c` do today;
the Pi gets `pi/host_circle.cpp`. Half a day, and it is the first commit
of the Pi branch rather than a separate refactor.

## Not a portability issue, but noted while reading

- `vicky_line` runs SHEILA and the raster IRQ for odd lines in lowres
  before returning — correct, and worth keeping that way.
- `io_frame_tick` is called from `vicky_end_frame`: the core's only
  video→io dependency. Fine; the Pi frontend calls the same functions.
- The keyboard FIFO is 256 bytes with no overflow signal. Typing via the
  Pi's USB keyboard (Circle's `CUSBKeyboardDevice`) arrives the same way
  as SDL keys, so no change.
