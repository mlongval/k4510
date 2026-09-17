#!/usr/bin/env python3
"""Generate the handbook's two reference lists from the machine's source.

  generated/commands.tex   every word the shell answers to, and where it lives
  generated/menu.tex       every row of the F7 menu, its k4510.cfg key, its choices

The register appendix is generated (mkregs.py) because a hand-kept copy of a
list the code already holds goes stale within a month.  These two lists are
the next most-changed things in the book, so they are made the same way:

  * the NAMES come from the code -- rom/kernal.c's shcmds[] table and the words
    shell_line() and nav() spell out, the programs in fs/SYSTEM/BIN, the
    languages in fs/LANG; the menu's rows from core/ui/menu.c, their keys,
    defaults and choices from core/ui/settings.c.
  * a command's one-line DESCRIPTION is written here, in DESC.  A command the
    code has and DESC does not fails the build, and so does a DESC entry for a
    command that is gone -- so a new word cannot reach a printing undescribed,
    and a removed one cannot linger in the book.

Run by make-guide.sh.  Never edit the generated files.
"""
import re, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = HERE / "generated"

# ---- the commands --------------------------------------------------------
# name -> (section, usage, description).  Usage is typed as shown; the first
# name of a group is the one that carries the entry.
SECTIONS = ["Files and directories", "Running things", "The screen",
            "The machine", "Memory", "Languages", "The Linux beneath",
            "Programs in /SYSTEM/BIN"]
DESC = {
    # files
    "DIR":     (0, "DIR [-a] [-l] [dir | pattern]", "List a directory. -a shows the hidden (dot) names, -l one to a line; a pattern matches here, * any run and ? any one character."),
    "CD":      (0, "CD [dir | .. | - | url]", "Change directory; the rest of the line is the name, spaces and all. Alone, to /. A tnfs:// or sftp:// URL goes there; - comes home."),
    "MKDIR":   (0, "MKDIR dir", "Make a directory."),
    "RMDIR":   (0, "RMDIR dir", "Remove an empty directory."),
    "RM":      (0, "RM [-f] name", "Move a file to /.TRASH, where DELETE -r gets it back. -f really removes it."),
    "RENAME":  (0, "RENAME [-f] old new", "Rename or move a file. Refuses to overwrite without -f."),
    "CP":      (0, "CP [-f] from to", "Copy a file (a URL may be the source). Refuses to overwrite without -f. Not COPY, which is memory."),
    "LOAD":    (0, "LOAD name [addr]", "Load a file into memory, at addr or at the address in its header."),
    "SAVE":    (0, "SAVE name from.to", "Write memory from.to to a file."),
    "XD":      (0, "XD name", "A file as a hex dump. Esc stops it."),
    "MOUNT":   (0, "MOUNT [url path]", "Alone, say where the machine's own disk really is -- the system in RAM, what you save on which partition -- and then list your mounts; otherwise show a tnfs://, http:// or https:// URL at a path of the disk."),
    "UMOUNT":  (0, "UMOUNT path", "Take a mount away."),
    # running
    "RUN":     (1, "RUN [name | addr]", "Run a program; a bare name does the same. RUN addr jumps there."),
    "EXEC":    (1, "EXEC name", "Run a text file as shell commands, one a line; # starts a comment. /STARTUP.BAT is run this way at power-on."),
    "SWAP":    (1, "SWAP [-k] command", "Put the whole 64 KB and the screen away, run the command on a clean machine, and give them back. -k keeps the screen the command left."),
    "ALIAS":   (1, "ALIAS [name [text]]", "List, define, or (name alone) remove an alias. Aliases are tried last, so none can hide a real command."),
    "ECHO":    (1, "ECHO text", "Print the text."),
    "HELP":    (1, "HELP", "The command summary: TYPE /SYSTEM/ETC/HELP."),
    # screen
    "CLS":     (2, "CLS", "Clear the text screen."),
    "CLG":     (2, "CLG", "Clear the bitmap over the text, whoever drew it."),
    "MODE":    (2, "MODE [n]", "Alone, say the mode; 0 640x480, 1 640x240, 2 320x240."),
    "COLOR":   (2, "COLOR fg [bg] [!]", "The text colours, as palette indices in hex. A pair the palette makes hard to read is refused, with one that reads suggested; ! has it anyway."),
    "PALETTE": (2, "PALETTE [LOAD name | SAVE name | RESET | i rr gg bb]", "The 256 colours: list them, set one, load a .PAL from /SYSTEM/ETC/PALETTES, save them, or put the machine's own back."),
    "BANNER":  (2, "BANNER", "Clear the screen and print the power-on banner again."),
    "CAPSLOCK":(2, "CAPSLOCK [ON | OFF]", "Toggle the caps lock: letters come up uppercase. Suspended while a program runs."),
    # machine
    "INFO":    (3, "INFO [-v]", "The machine's self-description, the clock in force included. -v names the exact build."),
    "TIME":    (3, "TIME", "The date and the time."),
    "HUSH":    (3, "HUSH", "Silence the OPL2 and the sound sequencer."),
    "DUMP":    (3, "DUMP [note | ON | OFF]", "Write the machine's whole state to dumps/ on the host. ON writes one every fifteen seconds."),
    "IDEA":    (3, "IDEA", "A brainshot: opens VI on a new one in /SYSTEM/BRAINSHOTS, with the machine as it was. No text argument: the shell line would cut it. *IDEA from a BASIC."),
    "RESET":   (3, "RESET", "Cold-start the machine, as the reset chord does."),
    # memory
    "MON":     (4, "MON [line]", "The machine monitor, Wozmon's grammar with 28-bit addresses. With a line, runs it and returns."),
    "FILL":    (4, "FILL from.to value", "Fill memory with a byte, by DMA."),
    "COPY":    (4, "COPY from.to dest", "Copy memory, by DMA. Not files: that is CP."),
    # languages
    "BBCBASIC":(5, "BBC", "BBC BASIC on the Tube (Chapter 6)."),
    "DOOM":    (5, "DOOM", "DOOM on the Tube, drawn on VICKY's bitmap (Chapter 6). Needs a WAD in /APPS/DOOM: tools/get-freedoom.sh fetches one."),
    "CPM":     (5, "CPM [command]", "CP/M 2.2 on the Z80 (Chapter 9). A command runs at boot."),
    "EHBASIC": (5, "EHBASIC", "Enhanced BASIC with the machine's graphics (Chapter 4)."),
    "MSBASIC": (5, "MSBASIC", "Microsoft's 6502 BASIC of 1977 (Chapter 5)."),
    "FORTH":   (5, "FORTH", "Tali Forth 2 (Chapter 7)."),
    "LOGO":    (5, "LOGO", "Turtle graphics (Chapter 8)."),
    "RX":      (5, "RX name", "Run a REXX script (Chapter 12); a bare HELLO runs HELLO.RX."),
    # linux
    "!":       (6, "!command  or  !", "Run a command on the Linux beneath, or (alone) open a shell there. Can be locked off."),
    "SSH":     (6, "SSH [user@]host", "An ssh session, through the Linux's ssh. Locked off with !."),
    "PAS":     (6, "PAS name", "Compile name.PAS here with Mad Pascal into name.prg."),
    "CC":      (6, "CC name", "Compile name.C here with cc65 into name.prg."),
    # programs
    "BANDS":     (7, "BANDS", "A program writing to the bottom status band."),
    "BOOK":      (7, "BOOK [n | word | page]", "This handbook, on the machine: the contents, chapter n, or the chapter whose title holds the word. Tab chooses a link, Enter follows it, Backspace comes back."),
    "BENCH":     (7, "BENCH", "About 25 s: frames per second and sound gaps at every clock step, to /SYSTEM/LOG/BENCH-NN.TXT."),
    "BUG":       (7, "BUG", "Asks seven questions about a fault and writes the report (Appendix B)."),
    "CHROUT":    (7, "CHROUT", "How fast the ROM's console prints."),
    "DELETE":    (7, "DELETE [-l | -r name | -e | name]", "The trash: list it, put a file back, empty it, or send a file there."),
    "EDIT":      (7, "EDIT [name]", "The modeless editor (Chapter 11)."),
    "KEYTEST":   (7, "KEYTEST", "Asks for every key and checks what arrives."),
    "KOMMANDER": (7, "KOMMANDER", "The two-panel file manager."),
    "MONITOR":   (7, "MONITOR", "The monitor as a program: MON, WOZ, FILL and COPY run it."),
    "MOUSETEST": (7, "MOUSETEST", "The mouse registers, live, with a sprite pointer."),
    "PADTEST":   (7, "PADTEST", "The held-keys register, live: a gamepad's first test."),
    "PETSCII":   (7, "PETSCII", "JIM, the terminal, speaking PETSCII."),
    "RANGER":    (7, "RANGER", "The miller-column file manager."),
    "SAY":       (7, "SAY text", "Prints its arguments: the smallest program there is."),
    "SETUP":     (7, "SETUP", "Measures this host thoroughly and keeps the clock it settles on."),
    "SPLIT":     (7, "SPLIT", "A split screen held by SHEILA: blitter lines above, four rows of text below, and how many lines a second."),
    "HEXED":     (7, "HEXED name | $address", "A hex editor: a file, loaded whole (8 MB at most), or memory by 28-bit address, changed live. Hex on the left, the code page on the right; go to, find, save, undo, the mouse."),
    "CODEPAGE":  (7, "CODEPAGE [437 | K4510]", "Alone, say which code page the machine speaks. 437 is IBM's, the default; K4510 gives 26 of its Greek and maths places to Western Europe's capitals, oe, the euro and German quotes. Remembered, as F12 -> Terminal -> Code page."),
    "FONTED":    (7, "FONTED [name.FNT] | -L name", "The font, edited where it lives, both sizes, every edit on the screen at once. -L loads a .FNT and leaves, for STARTUP.BAT."),
    "SUPERMON":  (7, "SUPERMON", "Jim Butterfield's monitor, grown up: an assembler and a 45GS02 disassembler."),
    "TELNET":    (7, "TELNET host [port]", "A terminal on a TCP connection. F12 hangs up."),
    "TYPE":      (7, "TYPE name", "A file, a screenful at a time; Esc or Q stops. A URL works."),
    "VI":        (7, "VI [name]", "The modal editor (Chapter 11)."),
    "STATUS":    (7, "STATUS", "The whole machine at a glance: its display and clock, how much of the 256 MB holds anything, the memory of the Linux beneath, where the system and your files really are and how much room is left, what is mounted, and the network by name and address."),
    "NVIM":      (7, "NVIM [name]", "Neovim, on the Linux beneath, set up for the machine: its colours, its languages, F9 to compile and F10 to run (Chapter 13)."),
    "PROG":      (7, "PROG [name]", "The programmer's front end: edit a C or Pascal program, compile it with F9, run it with Ctrl-F9, the compiler's messages under the text (Chapter 11)."),
}
# words that exist twice: the ROM command wins at the prompt, the program is
# still there for RUN.  Listed once, under the ROM's.
SHADOWED = {"BANNER"}


def die(msg):
    print(f"mkref: {msg}", file=sys.stderr)
    sys.exit(1)


def esc(s):
    rep = {"\\": r"\textbackslash{}", "_": r"\_", "#": r"\#", "$": r"\$", "%": r"\%",
           "&": r"\&", "~": r"\textasciitilde{}", "^": r"\textasciicircum{}",
           "{": r"\{", "}": r"\}"}
    return "".join(rep.get(c, c) for c in s)


def prose(s):
    """esc(), but a path or URL in the text is set with \\pth, which may break
    at / and . -- an A5 line cannot hold /SYSTEM/ETC/PALETTES unbroken."""
    parts = re.split(r"([a-z]+://[A-Za-z0-9_./-]*|/[A-Za-z0-9_][A-Za-z0-9_./-]*[A-Za-z0-9_/])", s)
    return "".join(f"\\pth{{{p}}}" if i % 2 else esc(p) for i, p in enumerate(parts))


def func_body(src, name):
    m = re.search(r"\n(?:static\s+)?void\s+" + name + r"\s*\([^)]*\)\s*\{", src)
    if not m:
        die(f"no {name}() in rom/kernal.c")
    i, depth = m.end(), 1
    while depth:
        depth += {"{": 1, "}": -1}.get(src[i], 0)
        i += 1
    return src[m.end():i]


def commands():
    src = (REPO / "rom/kernal.c").read_text()
    banks = {k: int(v) for k, v in re.findall(r"#define\s+(\w+_BANK)\s+(\d+)", src)}
    found = []                                  # (names, where)
    # the table
    t = re.search(r"shcmds\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not t:
        die("no shcmds[] table")
    groups = {}
    for name, bank, fn in re.findall(r"\{\s*n_(\w+),\s*(\w+),\s*(\w+)\s*\}", t.group(1)):
        bank = banks.get(bank, bank)
        groups.setdefault(fn, ([], bank))[0].append(name)
    for fn, (names, bank) in groups.items():
        if fn.startswith("mon_"):
            where = "runs MONITOR"
        else:
            where = "ROM" if str(bank) == "0" else f"ROM, bank {bank}"
        found.append((names, where))
    # the words written out
    for fname, where in (("nav", "ROM, bank 3"), ("shell_line", "ROM")):
        for line in func_body(src, fname).splitlines():
            names = re.findall(r'is_cmd\(&p,\s*"(\w+)"\)', line)
            if names:
                w = where
                if names[0] == "HELP":
                    w = "runs TYPE"
                elif names[0] in ("PAS", "CC", "SSH"):
                    w = "ROM, on the Linux"
                found.append((names, w))
    if "*p == '!'" not in func_body(src, "shell_line"):
        die("shell_line no longer takes '!' -- update mkref.py")
    found.append((["!"], "ROM, on the Linux"))
    # the programs
    progs = sorted(p.stem.upper() for p in (REPO / "fs/SYSTEM/BIN").glob("*.prg"))
    for p in progs:
        if p not in SHADOWED:
            found.append(([p], "/SYSTEM/BIN"))
    # the languages: a folder whose program has its own name
    for d in sorted((REPO / "fs/LANG").iterdir()):
        if (d / (d.name.lower() + ".prg")).exists():
            found.append(([d.name], f"/LANG/{d.name}"))

    have = {n[0] for n, _ in found}
    missing = sorted(have - DESC.keys())
    stale = sorted(DESC.keys() - have)
    if missing:
        die("no description for " + ", ".join(missing) + " -- add them to DESC in doc/guide/mkref.py")
    if stale:
        die("described but gone from the machine: " + ", ".join(stale) + " -- remove them from DESC")

    out = ["% generated by doc/guide/mkref.py from rom/kernal.c, fs/SYSTEM/BIN and fs/LANG -- never edit"]
    for si, sec in enumerate(SECTIONS):
        rows = sorted((n, w) for n, w in found if DESC[n[0]][0] == si)
        if not rows:
            continue
        out.append(f"\\section*{{{esc(sec)}}}")
        out.append("\\begin{cmdblock}")
        for names, where in rows:
            _, usage, text = DESC[names[0]]
            also = [n for n in names[1:]]
            if also:
                text += " Also " + ", ".join(also) + "."
            out.append(f"\\cmdentry{{{esc(usage)}}}{{{esc(where)}}}{{{prose(text)}}}")
        out.append("\\end{cmdblock}")
    (OUT / "commands.tex").write_text("\n".join(out) + "\n")
    return len(found)


# ---- the menu ------------------------------------------------------------
LINUX_ONLY = {"Host", "Shut down the computer"}


def c_enums(text):
    """every enum { A, B, ... } list: name -> index"""
    vals = {}
    for body in re.findall(r"enum\s*\w*\s*\{([^}]*)\}", re.sub(r"/\*.*?\*/", "", text, flags=re.S)):
        i = 0
        for item in body.split(","):
            item = item.strip()
            if not item:
                continue
            if "=" in item:
                item, v = [s.strip() for s in item.split("=", 1)]
                try:
                    i = int(v, 0)
                except ValueError:
                    pass
            vals[item] = i
            i += 1
    return vals


def menu():
    menu_c = (REPO / "core/ui/menu.c").read_text()
    set_c = (REPO / "core/ui/settings.c").read_text()
    set_h = (REPO / "core/ui/settings.h").read_text()
    enums = {}
    for h in list((REPO / "core").glob("**/*.h")):
        enums.update(c_enums(h.read_text(errors="replace")))
    # ...and a #define that is only another name for one of them, which is how
    # the menu's caps are written (VMODE_MENU_MAX = VMODE_360x270).  Without
    # this the cap read as 0 and every such row listed one choice: Resolution
    # said "640x480" and nothing else (found 2026-09-15).
    for h in list((REPO / "core").glob("**/*.h")):
        for name, val in re.findall(r"#define\s+(\w+)\s+(\w+)", h.read_text(errors="replace")):
            if val in enums and name not in enums:
                enums[name] = enums[val]
    ids = [m for m in re.findall(r"^\s*(SET_\w+),", re.search(r"typedef enum \{(.*?)\} set_id;", set_h, re.S).group(1), re.M)]
    set_nc = re.sub(r"/\*.*?\*/", "", set_c, flags=re.S)
    arrays = {n: re.findall(r'"([^"]*)"', body)
              for n, body in re.findall(r"static const char \*(?:const\s+)?(\w+)\[\]\s*=\s*\{(.*?)\};", set_nc, re.S)}
    # an ENUM the menu offers only the front of (settings_choices)
    offered = {sid: enums.get(lim, 0) + 1
               for sid, lim in re.findall(r"if \(id == (SET_\w+)\) return (\w+) \+ 1;", set_c)}
    # the clock's cap (settings.h CPUCLK_FASTEST): the menu offers the ladder from there
    capm = re.search(r"#define\s+CPUCLK_FASTEST\s+(\w+)", set_h)
    first_clock = enums.get(capm.group(1), 0) if capm else 0
    rows = re.findall(r'\{\s*"([\w.]+)",\s*"([^"]+)",\s*(ST_\w+),\s*(\w+),\s*(-?\w+),\s*(-?\w+),\s*\w+,\s*(\w+),', set_c)
    if len(rows) != len(ids):
        die(f"settings.c has {len(rows)} rows, settings.h {len(ids)} ids")
    desc = dict(zip(ids, rows))

    def choices(sid):
        key, label, typ, dflt, lo, hi, labels = desc[sid]
        d = enums.get(dflt, None)
        if d is None:
            try:
                d = int(dflt, 0)
            except ValueError:
                d = None
        if typ == "ST_BOOL":
            return "on, off", key, ("on" if d else "off")
        if typ == "ST_INT":
            return f"{lo} to {hi}", key, str(d)
        names = arrays.get(labels, [])
        dname = names[d] if d is not None and d < len(names) else ""   # the default by its index in the WHOLE list,
        if sid in offered:                                             # before any of it is trimmed (the cap made
            names = names[:offered[sid]]                               # 40.5 read as 10 MHz, four places along)
        if sid in ("SET_CPU_CLOCK", "SET_CPU_MEASURED") and first_clock:   # the steps above the cap are not offered
            names = names[first_clock:]
        return ", ".join(names), key, dname

    items = {n: re.findall(r'\{\s*"([^"]*)",\s*(MI_\w+)(?:,\s*([&\w]+))?(?:,\s*&?(\w+))?\s*\}', body)
             for n, body in re.findall(r"static const item_t (\w+)\[\]\s*=\s*\{(.*?)\n?\};", menu_c, re.S)}
    menus = {n: arr for n, arr in re.findall(r"static (?:const )?menu_t (\w+)\s*=\s*\{\s*\"[^\"]*\",\s*(\w+)", menu_c)}
    out = ["% generated by doc/guide/mkref.py from core/ui/menu.c and core/ui/settings.c -- never edit"]
    n = 0
    for label, kind, arg, sub in items["main_items"]:
        out.append(f"\\subsection*{{{esc(label)}}}")
        if label in LINUX_ONLY:
            out.append("\\menunote{Only on the K4510's own Linux.}")
        out.append("\\begin{cmdblock}")
        for rl, rk, ra, rs in items[menus[sub or arg.lstrip('&')]]:
            if rk == "MI_SEP":
                continue
            n += 1
            what = ""
            if rk == "MI_SETTING":
                ch, key, d = choices(ra)
                what = f"{ch}" + (f"; to begin with, {d}" if d else "")
                tag = key
            elif rk == "MI_SUBMENU":
                sm = items[menus[(rs or ra).lstrip('&')]]
                tag, what = "", f"{len(sm)} slots, each showing when it was written"
            elif rk == "MI_INFO":
                tag, what = "", "shows"
            else:
                tag, what = "", "does it"
            if rl in LINUX_ONLY:
                what += "; only on the K4510's own Linux"
            out.append(f"\\cmdentry{{{esc(rl)}}}{{{esc(tag)}}}{{{esc(what)}}}")
        out.append("\\end{cmdblock}")
    for l in LINUX_ONLY:
        if l not in menu_c:
            die(f"'{l}' is no longer in the menu -- update LINUX_ONLY")
    (OUT / "menu.tex").write_text("\n".join(out) + "\n")
    return n


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    c = commands()
    m = menu()
    print(f"mkref: {c} commands, {m} menu rows")
