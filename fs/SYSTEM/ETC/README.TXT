/SYSTEM/ETC -- files the machine looks for by name (BIN holds its tools, LOG what it writes).

chargen.bin
    A Commodore 64 character ROM, 4096 bytes: two 2048-byte sets, the
    second of them upper/lower case.  Put one here and the F7 menu's
    Screen font list offers "C64 chargen"; without it the entry reads
    "C64 chargen (none)" and selecting it leaves the built-in font.
    The glyphs are in PETSCII order and are rearranged into ASCII order
    on the way in, the same way the open-roms chargens are.

    No such ROM ships with the machine: it is Commodore's, not ours.
    Any C64 emulator's chargen.bin will do, or the license-clean
    open-roms one already in data/fonts/openroms/.
