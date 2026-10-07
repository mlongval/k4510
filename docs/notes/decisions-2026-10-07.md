# Decisions, 2026-10-07 (Doc, item by item)

From the restructure review, the trial-balloon audit and the video
foundations design.

## Restructure

1. `restructure` branch: smoke-test, then merge. Smoke test passed
   (Xvfb, 1920x1080: boots to the prompt, screenshot PNG valid).
   Merge left for Doc to run (`git merge --ff-only restructure`).
2. BENCH/BUG/SETUP "Raspberry Pi 3B+" text: fixed (455c490).

## Trial balloons

3. DOOM, the Apple IIe, DigiMAX: **cut**.
4. **Cut:** MS BASIC + chapter 05, TINY, BALLS, OPL2 demo, ANSIDEMO and
   SEGDEMO sources, rom/demo.bin, docs/PLATFORMER-PLAN.md.
   **Keep:** the ant farm sidebar.
5. CP/M + chapter 09: **sideline** (code kept; out of the default build,
   battery and handbook; `make cpm` brings it back; CPM says not fitted).
6. Navidrome radio sidebar: **sideline**.
7. Tek40xx: **sideline**.
8. Brainwatch: **cut**.
9. Sidebars: keep matrix, tetris, gradient; **cut** knot.
10. Dead weight: delete (ARM files in tube/src, forth/forth.lst,
    test/savershot.c, docs/check-edits.sh, make-pdf.sh, make-epub.sh, Pi
    photos, stale ignore rules/comments); old design docs and finished
    reviews to docs/history/; logotest twice and vikeystest missing in
    the Makefile fixed.

## Video foundations

The numbers below are the questions in docs/design-video-foundations.md
(branch `video-foundations`).

1. Every whole divisor of the canvas, not only powers of two. MODE 5,
   6, 7 keep meaning /1, /2, /4.
2. The smallest resolution K/OS runs in: 320x200.
3. Pixel cap: 1920x1080's worth (2,073,600) by default, **plus a system
   test** that measures what the hardware can draw and suggests (and
   can save, per host, as SETUP does for the clock) a lower cap so the
   CPU is not pinned.
4. 4K panels: accept the limit (1440x1080 /2 at most, HD text from
   720x540 /4 down). No GPU text layer for now.
5. Default canvas: 4:3.
6. k4510.cfg saves the divisor, not the size.
7. Odd sizes (1066x800, 341x256 ...): exact size, text centred with side
   padding.
8. K/OS text grids: at most **132 columns** (VT220 / Hercules wide);
   anything wider is not offered. The 255-column and 12,288-cell limits
   stay as they are.
9. MODE 0-2 stay for programs as software resolutions; K/OS always
   boots in an integer resolution (a saved 640x480 boots at /2).
   **TODO: adjust the Pascal graph unit** to the new modes later. LOGO
   is for fun: as it comes.
10. Scanlines: not now; a register bit reserved.
11. HD text: check once per character cell instead of once per pixel
    (same picture, less work), measured on the Dell.
12. Changing the canvas or divisor in F12 takes effect at once.

Software resolutions: as Doc leaned -- a program picks its own, within
the limits, and handles it.
