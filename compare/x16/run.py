#!/usr/bin/env python3
"""compare/x16/run.py -- the K4510 against the Commander X16, headless.

Runs the three tracks (README.md) on both machines and writes
RESULTS.md beside this file:

  1. each machine's own BASIC   x16/*.BAS on x16emu, the K4510 BASIC
                                twins in fs/LANG/BASIC/EX (and EX/X16)
  2. the same C, cc65 for both  c/*.c, -t cx16 and the K4510's recipe;
                                the K4510 at 40.5 MHz and again at 8 MHz
  3. graphics and sound         FILL, LINES, SPRITES, CHORD, in BASIC
  4. graphics in C              c/gfx.c: nine drawing tests, each machine's
                                own means (c/gfx.h), at 40.5 and 8 MHz

Every time is the machine's own 60 Hz tick (TI on the X16, FRAMES on
the K4510), read off the machine's text output; the host's clock is
never used and x16emu never runs with -warp.

    compare/x16/run.py                      everything
    compare/x16/run.py --only rf1,sieve     some, by name (lowercase; gfx = track 4)
    compare/x16/run.py --x16 DIR            x16emu + rom.bin live there
    compare/x16/run.py --no-k4510-8mhz      skip the 8 MHz K4510 pass

X16EMU_DIR in the environment also names the x16emu folder.  Default:
~/Projects/K4510-Personalities/work/x16emu-r49-official (the release
zip, unpacked), then the Personalities build in work/stage/x16.
Needs: cc65 (with the cx16 target), SDL2 (x16emu, run on the dummy
video and audio drivers), and the K4510 toolchain `make` finds.
Everything it builds goes to build/ (git-ignored); the K4510 C
programs are copied under fs/HOME/X16CMP for the run and removed.
"""
import argparse, os, re, shutil, subprocess, sys, time, datetime, struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BUILD = os.path.join(HERE, "build")
K_HOME = "fs/HOME/X16CMP"                       # the K4510 C programs, for the run
DUMMY = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
RESULT_RE = re.compile(r"RESULT\s+([A-Z0-9]+)\s+(-?[0-9.]+(?:E[-+]?\d+)?)\s*S", re.I)

# --- what runs -----------------------------------------------------------
# name -> (track, X16 BASIC file, K4510 directory, K4510 program, note)
BASIC = {
    "rf1": (1, "RF1.BAS", "/LANG/BASIC/EX", "RF1", "Rugg/Feldman 1: 1000 empty FOR/NEXT"),
    "rf2": (1, "RF2.BAS", "/LANG/BASIC/EX", "RF2", "RF 2: 1000 turns, K=K+1 / IF ... GOTO"),
    "rf3": (1, "RF3.BAS", "/LANG/BASIC/EX", "RF3", "RF 3: + A=K/K*K+K-K"),
    "rf4": (1, "RF4.BAS", "/LANG/BASIC/EX", "RF4", "RF 4: + A=K/2*3+4-5"),
    "rf5": (1, "RF5.BAS", "/LANG/BASIC/EX", "RF5", "RF 5: + GOSUB"),
    "rf6": (1, "RF6.BAS", "/LANG/BASIC/EX", "RF6", "RF 6: + FOR L=1 TO 5"),
    "rf7": (1, "RF7.BAS", "/LANG/BASIC/EX", "RF7", "RF 7: + M(L)=A"),
    "rf8": (1, "RF8.BAS", "/LANG/BASIC/EX", "RF8", "RF 8: 100 x K^2, LOG, SIN"),
    "sieve": (1, "SIEVE.BAS", "/LANG/BASIC/EX", "SIEVE", "Byte Sieve, 8191 flags, once"),
    "mandel": (1, "MANDEL.BAS", "/LANG/BASIC/EX", "DROGON", "Henderson's text Mandelbrot"),
    "strings": (1, "STRINGS.BAS", "/LANG/BASIC/EX/X16", "STRINGS", "strings: build, reverse, scan, STR$/VAL"),
    "fill": (3, "FILL.BAS", "/LANG/BASIC/EX/X16", "FILL", "200 full-screen fills, 320x240"),
    "lines": (3, "LINES.BAS", "/LANG/BASIC/EX/X16", "LINES", "500 lines (LINES) and 5000 points (POINTS)"),
    "sprites": (3, "SPRITES.BAS", "/LANG/BASIC/EX/X16", "SPRITES", "16 sprites x 100 MOVSPRs"),
    "chord": (3, "CHORD.BAS", "/LANG/BASIC/EX/X16", "CHORD", "a C major chord, not timed"),
}
CPROGS = {"csieve": "sieve", "cloop": "loop", "cmemcpy": "memcpy"}   # result name -> source
GFX = {   # track 4: c/gfx.c's tests, in its order -> what one pass is
    "clear": "the whole 320x240 bitmap filled",
    "rects": "64 filled rectangles, 1-128 x 1-96",
    "lines": "64 lines, ends anywhere",
    "pixels": "1024 single pixels",
    "image": "16 pictures of 32x32 from memory",
    "scroll": "the whole bitmap up one line",
    "text": "the 40x30 text layer written whole",
    "sprites": "32 sprites, each moved once",
    "palette": "240 palette entries",
}
K4510_HZ = 40500000
X16_HZ = 8000000


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def log(msg):
    print(msg, file=sys.stderr, flush=True)


# --- the Commander X16 ---------------------------------------------------
def find_x16(arg):
    cands = [arg, os.environ.get("X16EMU_DIR"),
             os.path.expanduser("~/Projects/K4510-Personalities/work/x16emu-r49-official"),
             os.path.expanduser("~/Projects/K4510-Personalities/work/stage/x16")]
    for d in cands:
        if not d:
            continue
        for exe in (os.path.join(d, "x16emu"), os.path.join(d, "bin", "x16emu")):
            rom = os.path.join(d, "rom.bin")
            if os.access(exe, os.X_OK) and os.path.exists(rom):
                return exe, rom
    return None, None


def x16_version(exe):
    r = sh([exe, "-h"], env=DUMMY)
    m = re.search(r"Commander X16 Emulator (\S+)", r.stdout + r.stderr)
    return m.group(1) if m else "?"


def run_x16(exe, rom, args, timeout, wav=None, gif=None):
    """Run x16emu until its echo says DONE (or the timeout); the cleaned text."""
    cmd = ["stdbuf", "-o0", exe, "-rom", rom, "-echo", "-run"] + args
    if gif:
        cmd += ["-gif", gif + ",wait"]
    if wav:
        cmd += ["-wav", wav + ",auto"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=DUMMY, cwd=BUILD)
    out, t0 = [], time.time()
    try:
        import select
        buf = b""
        while time.time() - t0 < timeout:
            r, _, _ = select.select([p.stdout], [], [], 0.5)
            if r:
                chunk = os.read(p.stdout.fileno(), 4096)
                if not chunk:
                    break
                buf += chunk
                if b"DONE" in buf:
                    time.sleep(0.3 if not wav else 1.0)   # let the recorder flush
                    break
            if p.poll() is not None:
                break
    finally:
        p.terminate()           # SDL turns SIGTERM into a quit: x16emu closes its .gif and .wav
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill(); p.wait()
    text = buf.decode("latin-1")
    text = re.sub(r"\\X[0-9A-F]{2}", "", text)
    return text


# --- the K4510 -----------------------------------------------------------
def run_k4510(keys, frames=36000, marker="DONE|ANY KEY|MACHINE TIME|Error", hz=None):   # MACHINE TIME: DROGON's last line
    env = dict(os.environ)
    if hz:
        env["K4510_CPU_HZ"] = str(hz)
    r = subprocess.run(["test/headless", "rom/kernal.bin", keys, str(frames), marker],
                       capture_output=True, text=True, cwd=REPO, env=env)
    m = re.search(r"\[(\d+) frames", r.stderr)
    return r.stdout, (int(m.group(1)) if m else None), ("marker seen" in r.stderr)


def shot_k4510(keys, frames, png):
    subprocess.run(["test/capture", "rom/kernal.bin", str(frames), png, keys],
                   capture_output=True, text=True, cwd=REPO)


def parse_k4510_basic(name, text):
    """The RESULT lines, or the older formats the EX programs print."""
    res = {k.upper(): float(v) for k, v in RESULT_RE.findall(text)}
    if res:
        return res
    m = re.search(r"THIS MACHINE, ONE RUN:\s*([0-9.]+)\s*S", text)
    if m:
        return {name.upper(): float(m.group(1))}
    m = re.search(r"TIME:\s*(\d+)\s*FRAMES", text)           # SIEVE
    if m:
        return {name.upper(): int(m.group(1)) / 60.0}
    m = re.search(r"TIME:\s*([0-9.]+)\s*SECONDS", text)      # DROGON
    if m:
        return {name.upper(): float(m.group(1))}
    return {}


# --- sound: is the .wav more than silence? --------------------------------
def wav_level(path):
    """(peak, samples) of a 16-bit .wav.  x16emu is stopped, not quit, so the
    header's sizes are still zero: read the PCM past the 44-byte header."""
    try:
        data = open(path, "rb").read()
        if len(data) < 48 or data[:4] != b"RIFF" or data[36:40] != b"data":
            return None
        pcm = data[44:len(data) - (len(data) - 44) % 2]
        s = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
        return max(abs(x) for x in s), len(s)
    except Exception:
        return None


def gif_to_png(gif):
    """The first frame of the X16's recording as PNG.  x16emu is stopped, not
    quit, so the .gif is what stdio flushed (the program records ~20 frames
    to make sure of a whole one); PIL reads a truncated file if asked."""
    if not gif or not os.path.exists(gif) or os.path.getsize(gif) == 0:
        return gif
    try:
        from PIL import Image, ImageFile
        ImageFile.LOAD_TRUNCATED_IMAGES = True
        png = gif[:-4] + ".png"
        im = Image.open(gif).convert("RGB")
        im.info.pop("transparency", None)    # the .gif's colour 0 is see-through: black, not a hole
        im.save(png)
        return png
    except Exception:
        return gif


def build_c(src, khome):
    """c/SRC.c for both: the X16's .prg (returned) and the K4510's, into khome."""
    c = os.path.join(HERE, "c", src + ".c")
    xprg = os.path.join(BUILD, "x16", src + ".prg")
    r = sh(["cl65", "-t", "cx16", "-O", "-o", xprg, c]); assert r.returncode == 0, r.stderr
    s = os.path.join(BUILD, "k4510", src + ".s"); o = os.path.join(BUILD, "k4510", src + ".o")
    kprg = os.path.join(khome, src + ".prg")
    for cmd in (["cc65", "-O", "-t", "none", "--cpu", "65c02", "-I", "demo", "-o", s, c],
                ["ca65", "--cpu", "65c02", "-o", o, s],
                ["ld65", "-C", "demo/prg.cfg", "-o", kprg, "demo/prg0.o", "demo/romcalls.o", o, "none.lib"]):
        r = sh(cmd, cwd=REPO); assert r.returncode == 0, (cmd, r.stderr)
    return xprg


def fmt_ms(x):
    if x is None:
        return "--"
    ms = x * 1000
    return "%.1f" % ms if ms >= 10 else "%.2f" % ms if ms >= 0.1 else "%.4f" % ms


def fmt(x):
    return "--" if x is None else ("%.2f" % x if x >= 0.1 else "%.4f" % x)


def ratio(a, b):
    return "--" if not a or not b or b == 0 else "%.1fx" % (a / b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x16", help="folder with x16emu and rom.bin")
    ap.add_argument("--only", help="comma-separated names (rf1,sieve,csieve,...)")
    ap.add_argument("--no-k4510-8mhz", action="store_true")
    ap.add_argument("--out", default=os.path.join(HERE, "RESULTS.md"))
    a = ap.parse_args()
    only = set(a.only.lower().split(",")) if a.only else None
    want = lambda n: only is None or n in only

    os.makedirs(os.path.join(BUILD, "x16"), exist_ok=True)
    os.makedirs(os.path.join(BUILD, "k4510"), exist_ok=True)
    os.makedirs(os.path.join(BUILD, "shots"), exist_ok=True)

    log("building the K4510 side (make) ...")
    r = sh(["make", "-s", "test/headless", "test/capture", "pascal-prgs", "demo/prg0.o", "demo/romcalls.o"], cwd=REPO)
    if r.returncode:
        log(r.stdout[-2000:] + r.stderr[-2000:]); sys.exit("make failed")

    exe, rom = find_x16(a.x16)
    if not exe:
        sys.exit("no x16emu: pass --x16 DIR (x16emu + rom.bin) or set X16EMU_DIR")
    x16ver = x16_version(exe)
    log("x16emu %s: %s" % (x16ver, exe))

    rows_basic = {}     # name -> dict(result -> (x16 s, k4510 s))
    shots = {}
    wavnote = ""
    # ---- tracks 1 and 3: BASIC
    for name, (track, xfile, kdir, kprog, note) in BASIC.items():
        if not want(name):
            continue
        log("BASIC %-8s X16 ..." % name)
        gif = os.path.join(BUILD, "shots", "x16-%s.gif" % name) if track == 3 and name != "chord" else None
        wav = os.path.join(BUILD, "shots", "x16-%s.wav" % name) if name == "chord" else None
        xt = run_x16(exe, rom, ["-bas", os.path.join(HERE, "x16", xfile)], timeout=900, gif=gif, wav=wav)
        xres = {k.upper(): float(v) for k, v in RESULT_RE.findall(xt)}
        open(os.path.join(BUILD, "x16-%s.txt" % name), "w").write(xt)
        if wav and os.path.exists(wav):
            lv = wav_level(wav)
            wavnote = ("x16 CHORD: %s, peak %d of 32767 (%d samples)" % (os.path.basename(wav), lv[0], lv[1])) if lv else "x16 CHORD: no usable wav"
        log("BASIC %-8s K4510 ..." % name)
        keys = "CD %s\nRUN %s\n" % (kdir, kprog)     # RUN: FILL alone is the shell's memory fill
        kt, kframes, seen = run_k4510(keys)
        open(os.path.join(BUILD, "k4510-%s.txt" % name), "w").write(kt)
        kres = parse_k4510_basic(name, kt) if seen else {}     # keyed by the comparison name: DROGON is MANDEL here
        if track == 3 and name != "chord" and kframes:
            png = os.path.join(BUILD, "shots", "k4510-%s.png" % name)
            shot_k4510(keys, kframes + 20, png)
            shots[name] = (gif_to_png(gif), png)
        for key in sorted(set(xres) | set(kres)):
            rows_basic.setdefault(name, {})[key] = (xres.get(key), kres.get(key))
        if name == "chord":
            rows_basic[name] = {"CHORD": ("OK" if "CHORD OK" in xt else None, "OK" if "CHORD OK" in kt else None)}
        log("   %s" % rows_basic.get(name))

    # ---- track 2: C
    rows_c = {}     # result -> (x16, k4510 40.5, k4510 8)
    cnames = [n for n in CPROGS if want(n) or want(CPROGS[n])]
    if cnames:
        khome = os.path.join(REPO, K_HOME)
        os.makedirs(khome, exist_ok=True)
        try:
            for res, src in CPROGS.items():
                if res not in cnames:
                    continue
                xprg = build_c(src, khome)
                log("C %-8s X16 ..." % src)
                xt = run_x16(exe, rom, ["-prg", xprg], timeout=900)
                open(os.path.join(BUILD, "x16-c-%s.txt" % src), "w").write(xt)
                xres = {k.upper(): float(v) for k, v in RESULT_RE.findall(xt)}
                keys = "CD /HOME/X16CMP\nRUN %s\n" % src.upper()
                log("C %-8s K4510 40.5 MHz ..." % src)
                kt, _, seen = run_k4510(keys, marker="done|DONE|Error")
                open(os.path.join(BUILD, "k4510-c-%s.txt" % src), "w").write(kt)
                kres = {k.upper(): float(v) for k, v in RESULT_RE.findall(kt)} if seen else {}
                k8 = {}
                if not a.no_k4510_8mhz:
                    log("C %-8s K4510 8 MHz ..." % src)
                    kt8, _, seen8 = run_k4510(keys, hz=X16_HZ, marker="done|DONE|Error")
                    open(os.path.join(BUILD, "k4510-c8-%s.txt" % src), "w").write(kt8)
                    k8 = {k.upper(): float(v) for k, v in RESULT_RE.findall(kt8)} if seen8 else {}
                for key in sorted(set(xres) | set(kres) | set(k8)):
                    rows_c[key] = (xres.get(key), kres.get(key), k8.get(key))
                log("   %s" % {k: rows_c[k] for k in rows_c if k in xres or k in kres})
        finally:
            shutil.rmtree(khome, ignore_errors=True)

    # ---- track 4: graphics in C
    rows_g = {}     # test -> (x16, k4510 40.5, k4510 8, passes x16, passes k4510)
    gshots = None
    if want("gfx"):
        khome = os.path.join(REPO, K_HOME)
        os.makedirs(khome, exist_ok=True)
        pass_re = re.compile(r"RESULT\s+([A-Z]+)\s+([0-9.]+)\s*S,\s*PASSES\s+(\d+)", re.I)
        parse = lambda t: {k.lower(): (float(v), int(n)) for k, v, n in pass_re.findall(t)}
        try:
            xprg = build_c("gfx", khome)
            log("C gfx      X16 ...")
            gif = os.path.join(BUILD, "shots", "x16-gfx.gif")
            xt = run_x16(exe, rom, ["-prg", xprg], timeout=900, gif=gif)
            open(os.path.join(BUILD, "x16-c-gfx.txt"), "w").write(xt)
            xres = parse(xt)
            keys = "CD /HOME/X16CMP\nRUN GFX\n"
            log("C gfx      K4510 40.5 MHz ...")
            kt, kframes, seen = run_k4510(keys, marker="done|Error")
            open(os.path.join(BUILD, "k4510-c-gfx.txt"), "w").write(kt)
            kres = parse(kt) if seen else {}
            if kframes:     # the test card is up for the 120 frames before the results
                png = os.path.join(BUILD, "shots", "k4510-gfx.png")
                shot_k4510(keys, kframes - 60, png)
                gshots = (gif_to_png(gif), png)
            k8 = {}
            if not a.no_k4510_8mhz:
                log("C gfx      K4510 8 MHz ...")
                kt8, _, seen8 = run_k4510(keys, hz=X16_HZ, marker="done|Error")
                open(os.path.join(BUILD, "k4510-c8-gfx.txt"), "w").write(kt8)
                k8 = parse(kt8) if seen8 else {}
            for t in GFX:
                x, k, q = xres.get(t), kres.get(t), k8.get(t)
                rows_g[t] = (x and x[0], k and k[0], q and q[0], x and x[1], k and k[1])
            log("   %s" % rows_g)
        finally:
            shutil.rmtree(khome, ignore_errors=True)

    # ---- the table
    today = datetime.date.today().isoformat()
    head = sh(["git", "rev-parse", "--short", "HEAD"], cwd=REPO).stdout.strip()
    cc = sh(["cc65", "--version"]); ccv = (cc.stdout + cc.stderr).strip().splitlines()[0] if (cc.stdout + cc.stderr).strip() else "cc65"
    L = []
    L.append("# K4510 vs Commander X16 -- results\n")
    L.append("Written by `compare/x16/run.py` on %s, K4510 %s, x16emu %s,\n%s. Seconds of each machine's own clock (60 Hz ticks: TI on\nthe X16, FRAMES on the K4510). Read README.md before comparing\nanything: the two BASICs are not the same kind of thing.\n" % (today, head, x16ver, ccv))
    L.append("## Track 1: each machine's own BASIC\n")
    L.append("X16 BASIC is interpreted (CBM BASIC V2 + X16 words, 65C02 at\n8 MHz). K4510 BASIC is compiled to Mad Pascal (45GS02 at 40.5 MHz).\n")
    L.append("| Program | What | X16 (s) | K4510 (s) | X16 / K4510 |")
    L.append("|---|---|---:|---:|---:|")
    for name, (track, xfile, kdir, kprog, note) in BASIC.items():
        if track != 1 or name not in rows_basic:
            continue
        for key, (x, k) in rows_basic[name].items():
            L.append("| %s | %s | %s | %s | %s |" % (key, note, fmt(x), fmt(k), ratio(x, k)))
    L.append("")
    L.append("## Track 2: the same C, built with cc65 for both\n")
    L.append("One source each (`c/`), `cl65 -t cx16 -O` for the X16 and the\nK4510's own recipe (`cc65 -t none --cpu 65c02 -O`, tools/k4510-cc).\nThe K4510 column at 8 MHz is the harness run with K4510_CPU_HZ=8000000:\nthe same clock as the X16, so the CPUs meet like for like.\n")
    L.append("| Program | X16 8 MHz (s) | K4510 40.5 MHz (s) | K4510 8 MHz (s) | X16 / K4510@40.5 | X16 / K4510@8 |")
    L.append("|---|---:|---:|---:|---:|---:|")
    for key, (x, k, k8) in rows_c.items():
        L.append("| %s | %s | %s | %s | %s | %s |" % (key, fmt(x), fmt(k), fmt(k8), ratio(x, k), ratio(x, k8)))
    L.append("")
    L.append("## Track 3: graphics and sound, from BASIC\n")
    L.append("VERA from X16 BASIC (RECT, LINE, PSET, SPRITE/MOVSPR, FMCHORD,\nPSGCHORD) against VICKY and MELODY from K4510 BASIC (BOX, LINE,\nPLOT, SPRDEF/MOVSPR, PLAY). The BASIC's own speed is part of\nevery figure here.\n")
    L.append("| Program | What | X16 (s) | K4510 (s) | X16 / K4510 |")
    L.append("|---|---|---:|---:|---:|")
    for name, (track, xfile, kdir, kprog, note) in BASIC.items():
        if track != 3 or name not in rows_basic:
            continue
        for key, (x, k) in rows_basic[name].items():
            if isinstance(x, str) or isinstance(k, str):
                L.append("| %s | %s | %s | %s | -- |" % (key, note, x or "--", k or "--"))
            else:
                L.append("| %s | %s | %s | %s | %s |" % (key, note, fmt(x), fmt(k), ratio(x, k)))
    L.append("")
    if rows_g:
        L.append("## Track 4: graphics in C, each machine's own means\n")
        L.append("`c/gfx.c`, one source, each drawing word written the way that\nmachine does it best from C (`c/gfx.h`): the X16 KERNAL's GRAPH\nroutines and VERA's data port; the K4510's blitter, DMA and far\npokes. Milliseconds for one pass; a test repeats its pass for two\nseconds of the machine's clock. The K4510's blitter and DMA finish\nin the write that starts them, so its figures are the CPU setting\nregisters: read README.md before quoting CLEAR or SCROLL.\n")
        L.append("| Test | One pass | X16 8 MHz (ms) | K4510 40.5 MHz (ms) | K4510 8 MHz (ms) | X16 / K4510@40.5 | X16 / K4510@8 |")
        L.append("|---|---|---:|---:|---:|---:|---:|")
        for t, note in GFX.items():
            if t in rows_g:
                x, k, k8, _, _ = rows_g[t]
                L.append("| %s | %s | %s | %s | %s | %s | %s |" % (t.upper(), note, fmt_ms(x), fmt_ms(k), fmt_ms(k8), ratio(x, k), ratio(x, k8)))
        L.append("")
        if gshots:
            L.append("Test cards in `build/shots/`: %s, %s.\n" % (os.path.basename(gshots[0] or "-"), os.path.basename(gshots[1])))
    if wavnote:
        L.append("Sound check: %s. The K4510 harness has no audio device;\nits CHORD is checked by running, not by listening.\n" % wavnote)
    if shots:
        L.append("Screenshots in `build/shots/` (not tracked): " + ", ".join("%s (%s, %s)" % (n, os.path.basename(shots[n][0] or "-"), os.path.basename(shots[n][1])) for n in shots) + ".\n")
    open(a.out, "w").write("\n".join(L))
    log("wrote " + a.out)
    print("\n".join(L))


if __name__ == "__main__":
    main()
