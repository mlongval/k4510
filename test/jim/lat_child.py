#!/usr/bin/env python3
"""lat_child.py LOG -- the program on the Terminal screen for latrun.sh: raw
mode; each key read is logged (its CLOCK_MONOTONIC ns) and answered at once
with a short echo, whose write time is logged too.  The emulator's
K4510_LATLOG says when those bytes were read and shown."""
import os, sys, time, tty
log = open(sys.argv[1], 'w', buffering=1)
tty.setraw(0)
os.write(1, b'\x1b[2J\x1b[Hlatency child ready\r\n')
log.write('READY\n')
col = 0
while True:
    b = os.read(0, 64)
    tr = time.monotonic_ns()
    if not b: break
    for _ in b:
        col = (col + 1) % 60
        os.write(1, b'\x1b[5;%dH#' % (col + 1))      # one cell, in place: no scrolling
        tw = time.monotonic_ns()
        log.write(f'K {tr} {tw}\n')
