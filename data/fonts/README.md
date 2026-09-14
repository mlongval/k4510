# data/fonts — the machine's one font

The K4510 has one screen font: **unscii**, by Ville-Matias Heikkilä
("Viznut"), public domain.  Doc, 2026-09-14: "pick one font and jettison
all the rest".

    unscii/unscii-8.hex        upstream, unchanged
    unscii/font8-unscii.bin    2048 bytes: 256 glyphs x 8 rows, CP437 order,
                               MSB-first.  Loaded at $010000; the 240-line
                               modes (80x30, 40x30) and the F7 menu.
    unscii/unscii-16.hex       upstream, unchanged
    unscii/font16-unscii.bin   4096 bytes: 256 glyphs x 16 rows, same order.
                               Loaded at $010800; MODE 0, 640x480, which is
                               80x30 in 8x16 cells; also the side panel and
                               the F7 menu at that size.

The two `.bin` files are kept as generated.  The generator
(`hex2chargen.py`) was removed 2026-09-14 with the other fonts; the
`.hex` sources stay, so the glyphs can always be rebuilt.  Provenance
and hashes: `unscii/VENDORED-FROM.txt`.

Removed 2026-09-14: the Linux kernel 8x8 font (`data/font8.bin`), the
MEGA65 open-roms chargen and PXLfont, BESCII, the twelve ZX Origins faces,
and the import of a Commodore `chargen.bin`.  The history keeps them.
