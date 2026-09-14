# Languages in PROG (and VI)

How a programming language plugs into the edit / compile / run / fix cycle
of PROG and VI, written 2026-09-14 when REXX became the third language
(Doc: "document the edit/compile or interpret/debug cycle for the PROG
program in case we add other programming languages").

Both editors share one engine, `demo/ed.h`; everything below lives there
unless it says otherwise. PROG adds tabs, menus and projects (`demo/prog.c`);
VI adds `:make`, `:run`, `:cn`, `:cp`.

## The cycle

```
 edit ──F9──▶ save (only if changed) ──▶ build ──▶ MAKE.ERR ──▶ error list
   ▲                                                              │
   └──────────── F4 / Shift-F4: next / previous message ◀─────────┘

 Ctrl-F9 = F9, then SWAP -k <program>, "a key returns", then back
```

| key (PROG) | VI | what the engine does |
|---|---|---|
| F9 | `:make` | `do_make()`: save if dirty, run the language's build line through `rom_shell`, read MAKE.ERR, go to the first error |
| Ctrl-F9 | `:run` | `do_run()`: `do_make()`, then `SWAP -k` the program; wait for a key; reload the file (the program may have used its far memory) |
| F4 / Shift-F4 | `:cn` / `:cp` | `err_go()`: to the next / previous entry; one in another file opens a tab (PROG) |

## The one contract: /SYSTEM/LOG/MAKE.ERR

A language never talks to the editor directly. It leaves its messages in
`/SYSTEM/LOG/MAKE.ERR`, one per line:

```
FILE:LINE:COL:KIND:TEXT
```

* **FILE**: the file's name *without its directory*; the editor looks for
  it in the directory it built in (`ed_mkdir`). `-` for no file of yours
  (the linker, a missing tool).
* **LINE**, **COL**: 1-based; `0` when unknown (then the editor goes to
  the line and leaves the column alone).
* **KIND**: `E` error, `W` warning, `I` information (shown as the build's
  result: "1 compiled, 1 kept"), `F` a file-level note.
* **TEXT**: the message, to the end of the line.

Two rules follow:

1. **Empty the file when a build or run starts.** An old error must not
   survive a clean run. `k4510-cc` does `: > "$ERR"`; `rx.prg` saves a
   zero-length MAKE.ERR before the script runs.
2. **A build fails when there is an `E`.** `do_make` counts entries: no
   errors (only W and I) and exit status 0 means success.

`tools/k4510-errfmt` turns compiler logs into this form (cc65, ca65, ld65,
Mad Pascal, MADS). A new host compiler needs only a case there.

## Two kinds of language

### Compiled on the host (C, Pascal)

The compiler runs on Linux, reached from the machine through the host
shell (`!`): the K/OS words `CC` and `PAS` are
`cmd_compile("k4510-cc" | "k4510-pas", …)` in `rom/kernal.c`.

* `tools/k4510-cc NAME` / `tools/k4510-pas NAME`: compile NAME.C / NAME.PAS
  into `name.prg` beside it, log through `k4510-errfmt` into MAKE.ERR, and
  exit with the compiler's status (the ROM hands it back as `SHELL_RC`).
* Projects: `-p PROJECT.K4P` (see below).
* Run: `SWAP -k name` (the .prg, lowercase, without extension).

### Interpreted on the machine (REXX)

The interpreter is a guest program (`fs/LANG/RX/rx.prg`, source
`demo/rexx.c`). There is nothing to build.

* F9 / `:make` only saves (`do_make` returns 1 with "saved -- REXX has
  nothing to compile").
* Ctrl-F9 / `:run` runs `SWAP -k RX file`. The interpreter writes MAKE.ERR
  itself: empty at the start; on a fatal error `die()` prints
  `RX: line N: message` *and* writes `NAME:N:0:E:message`.
* After the key, `do_run` reads MAKE.ERR (`err_load`) and goes to the
  error (`err_go`), so a runtime error lands on its line like a
  compile error.

## Adding a language: the checklist

1. **Recognise the extension**: `compiler()` in `demo/ed.h` returns the
   machine's word for it (`"CC"`, `"PAS"`, `"RX"`). The message in
   `do_make` ("no compiler for this file (.C, .PAS, .RX)") lists them.
2. **Compiled on the host?** Add `tools/k4510-<lang>` (copy
   `k4510-cc`'s shape: empty MAKE.ERR, compile, pipe the log through
   `k4510-errfmt`, exit with the status), a case in `k4510-errfmt` for
   its message form, and a K/OS word in `rom/kernal.c` next to `CC` and
   `PAS`. The engine's default build line is `WORD name`, so nothing in
   `ed.h` changes beyond step 1. The .prg must come out as `name.prg`
   beside the source, or the run line needs its own case in `do_run`.
3. **Interpreted on the machine?** Treat it like `"RX"` in `do_make`
   (save only) and `do_run` (`SWAP -k WORD file`, then `err_load`,
   `err_go`). The interpreter must empty MAKE.ERR when it starts and
   write one `E` line on a fatal error. Keep the name without its
   directory and the line 1-based; `ed_mkdir` is set to the file's
   directory by `mkdir_of_name()`.
4. **Projects** (optional): `LANG=` in PROJECT.K4P picks the build word
   in `pj_arm()` (`demo/prog.c`), which sets `ed_mkline` (the build
   line, e.g. `CC -p /GAME/PROJECT.K4P`), `ed_mkdir` and `ed_runname`
   (the program to run). The tool takes `-p FILE` and reads `NAME`,
   `LANG`, `SRC`, `MAIN`, `OUT`. An interpreted language takes
   precedence over a project (the `.RX` check comes first).
5. **PROG's help**: the F1 text in `demo/prog.c` (`helptext[]`) and the
   File > New project choices if it gets projects.
6. **A remote test**: `test/remote/<lang>.k4r`, the shape of
   `prog.k4r` / `rexx.k4r`: a file with an error, F9 or Build > Compile
   and run, expect `error 1 of`, fix, run again.
7. **The handbook**: the language's chapter and the PROG section.

## Things that bite

* **`rom_shell` strings must be below $A000.** During a ROM call
  $A000–$CFFF is ROM, so the build line lives in BSS (`shline`).
* **SWAP keeps 64 KB, not far memory.** The text lives in far memory, so
  `do_run` reloads the file after the program; the other tabs are read
  back by `reload_others()` in PROG.
* **Save only what changed.** Re-saving an unchanged file makes it newer
  than its object, and a project rebuilds it for nothing.
* **Byte returns from the ROM**: `zp_out` ends `ldx #0`; a caller that
  tests an `unsigned char` result through a wider type sees X otherwise.
