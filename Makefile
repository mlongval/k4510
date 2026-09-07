# K4510 -- the machine, built without VICE.
#   make            -> build everything
#   make test       -> run the CPU wrapper test
# `all` is below, after the variables it needs; say so here, because the
# core/build.h rule comes first in the file and would otherwise become the
# default goal -- which made a bare `make` build one header and stop.
.DEFAULT_GOAL := all

CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wno-unused-function -Icore
# The exact build, stamped into the machine's version register so a BUG report
# can name the commit it came from.  Only core/io.o pays for it, so a new commit
# does not rebuild the world.  Must fit 15 characters.
K4510_BUILD := 0.5-$(shell git rev-parse --short=7 HEAD 2>/dev/null || echo nogit)$(shell git diff --quiet HEAD 2>/dev/null || echo +)
core/build.h: FORCE
	@printf '/* generated: the commit this build came from */\n#define K4510_BUILD "%s"\n' '$(K4510_BUILD)' > $@.tmp; \
	 cmp -s $@.tmp $@ 2>/dev/null || mv $@.tmp $@; rm -f $@.tmp
core/io.o: core/build.h
.PHONY: FORCE
FORCE:

OPL2_OBJS = core/opl2/fmopl.o core/opl2.o core/vice_clk.o core/sndq.o core/audio.o
CORE_OBJS = core/xemu/cpu65.o core/mem.o core/io.o core/vicky.o core/net.o core/net_posix.o core/term.o core/state.o core/hostid.o core/ui/settings.o core/ui/menu.o core/ui/ui_draw.o sdl/host_posix.o $(OPL2_OBJS)
LDLIBS  = -lm -lutil
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

ACME ?= $(HOME)/.local/bin/acme

all: rom/wozmon.bin rom/demo.bin rom/kernal.bin fs/PRG/balls.prg fs/PRG/cube.prg fs/PRG/mandel.prg fs/PRG/keytest.prg fs/PRG/sieve.prg fs/PRG/chrout.prg fs/PRG/segdemo.prg fs/PRG/opl2.prg fs/PRG/say.prg fs/PRG/telnet.prg fs/PRG/edit.prg fs/PRG/vi.prg fs/PRG/logo.prg fs/PRG/bug.prg fs/PRG/bench.prg fs/PRG/setup.prg fs/PRG/kommander.prg fs/PRG/ranger.prg fs/PRG/delete.prg fs/PRG/tiny.prg fs/PRG/lode.prg fs/PRG/bomber.prg fs/PRG/ansidemo.prg fs/PRG/petscii.prg fs/PRG/bands.prg fs/PRG/oplplay.prg fs/PRG/padtest.prg fs/PRG/mousetest.prg fs/PRG/skyfire.prg fs/PRG/fluffy.prg fs/PRG/chess.prg pascal-prgs fs/EHBASIC/ehbasic.prg fs/MSBASIC/msbasic.prg fs/FORTH/forth.prg cpm/runcpm test/mathtest test/termtest test/uitest test/statetest test/capture test/headless test/fstest test/romtest test/cputest test/woztest test/maptest test/banktest test/dmatest test/vickytest test/seqtest sdl/k4510

rom/wozmon.bin: rom/wozmon.a
	$(ACME) --cpu m65 -o $@ $<

rom/demo.bin: rom/demo.a
	$(ACME) --cpu m65 -o $@ $<

# System ROM: C with cc65 (65C02 output is a subset of the 45GS02)
rom/kernal.bin: rom/kernal.c rom/crt0.s rom/k4510.cfg
	cc65 -O -t none --cpu 65c02 --local-strings -o rom/kernal.s rom/kernal.c
	ca65 --cpu 65c02 -o rom/kernal.o rom/kernal.s
	ca65 --cpu 65c02 -o rom/crt0.o rom/crt0.s
	ld65 -C rom/k4510.cfg -o $@ rom/crt0.o rom/kernal.o none.lib -m rom/kernal.map

test/seqtest: test/seqtest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
test/statetest: test/statetest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/uitest: test/uitest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/termtest: test/termtest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/fstest: test/fstest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/romtest: test/romtest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/bench: test/bench.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/headless: test/headless.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/capture: test/capture.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

rom: rom/wozmon.bin rom/demo.bin rom/kernal.bin

core/xemu/cpu65.o: core/xemu/cpu65.c core/xemu/cpu65.h core/xemu/emutools_basicdefs.h core/hypervisor.h
	$(CC) $(CFLAGS) -c -o $@ $<

core/mem.o: core/mem.c core/mem.h core/host.h core/xemu/emutools_basicdefs.h
sdl/host_posix.o: sdl/host_posix.c core/host.h
core/vicky.o: core/vicky.c core/vicky.h core/mem.h
core/io.o: core/io.c core/io.h core/mem.h core/vicky.h core/opl2.h core/audio.h core/net.h core/term.h
core/net.o: core/net.c core/net.h core/net_plat.h core/mem.h
core/net_posix.o: core/net_posix.c core/net_plat.h
core/term.o: core/term.c core/term.h core/mem.h core/io.h
core/state.o: core/state.c core/state.h core/mem.h
core/mem.o: core/state.h
core/vicky.o: core/state.h
core/io.o: core/state.h
core/term.o: core/state.h
core/ui/settings.o: core/ui/settings.c core/ui/settings.h
core/ui/menu.o: core/ui/menu.c core/ui/menu.h core/ui/settings.h core/ui/ui_draw.h core/io.h
core/ui/ui_draw.o: core/ui/ui_draw.c core/ui/ui_draw.h
core/io.o: core/ui/menu.h
core/opl2.o: core/opl2.c core/opl2.h core/vice_clk.h core/sndq.h core/opl2/fmopl.h
core/audio.o: core/audio.c core/audio.h core/opl2.h core/vice_clk.h core/sndq.h
core/sndq.o: core/sndq.c core/sndq.h
core/vice_clk.o: core/vice_clk.c core/vice_clk.h core/opl2/alarm.h
# MAME's fmopl.c, by way of VICE, used unaltered; core/opl2/ holds it and the
# shim headers that stand in for the emulator it came from.
core/opl2/fmopl.o: core/opl2/fmopl.c
	$(CC) $(CFLAGS) -Icore/opl2 -Wno-unused-parameter -c -o $@ $<

sdl/k4510: sdl/main.c $(CORE_OBJS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS) $(LDLIBS)
	ln -sf sdl/k4510 k4510          # so it starts as ./k4510 from the repo root

test/cputest: test/cputest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
	./test/nettest.sh

test/woztest: test/woztest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/banktest: test/banktest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/maptest: test/maptest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/dmatest: test/dmatest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/vickytest: test/vickytest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test/mathtest: test/mathtest.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)


# Are the tracked binaries what their sources actually produce?  They are
# tracked because p15 has no cc65 and the Pi card needs them, which means a
# source change with no rebuild ships a binary that disagrees with its own
# source -- and every machine that rebuilds it then reports a dirty tree.
# That happened: romcalls.s grew three bytes and only bug.prg was rebuilt.
.PHONY: check-artifacts
# Only what cc65 alone can build: acme (wozmon, demo) and 64tass (forth) are
# not on every build host, and this must run wherever the tests do.
check-artifacts: $(DEMOS) fs/EHBASIC/ehbasic.prg fs/MSBASIC/msbasic.prg rom/kernal.bin
	@git diff --quiet -- fs/PRG fs/EHBASIC fs/MSBASIC rom/kernal.bin rom/wozmon.bin rom/demo.bin || { \
	  echo "STALE: these tracked binaries are not what their sources build:"; \
	  git diff --name-only -- fs/PRG fs/EHBASIC fs/MSBASIC rom/kernal.bin rom/wozmon.bin rom/demo.bin | sed 's/^/  /'; \
	  echo "Rebuild them and commit, or the next machine to build will look dirty."; \
	  exit 1; }
	@echo "check-artifacts: tracked binaries match their sources"

test: check-artifacts fs/PRG/ranger.prg fs/PRG/delete.prg test/cputest test/woztest test/maptest test/banktest test/dmatest test/vickytest test/seqtest test/fstest test/termtest test/uitest test/statetest test/romtest test/mathtest rom/wozmon.bin rom/kernal.bin
	./test/cputest
	./test/woztest
	./test/maptest
	./test/banktest
	./test/dmatest
	./test/vickytest
	./test/seqtest
	./test/fstest
	./test/termtest
	./test/uitest
	./test/statetest
	./test/pastest.sh
	./test/basictest.sh
	./test/msbasictest.sh
	./test/jimtest.sh
	./test/opltest.sh
	./test/palettetest.sh
	./test/romtest
	./test/mathtest
	./test/rangertest.sh
	./test/deletetest.sh
	./test/bangtest.sh

clean: clean-demos
# Only the .s files cc65 generates -- one per .c, plus the two built under a
# different name.  `rm -f demo/*.s` used to be here, and it deleted the
# HAND-WRITTEN sources (prg0.s, romcalls.s, the *-header.s files): a clean
# checkout could not be built after a `make clean`.
GEN_S = $(patsubst demo/%.c,demo/%.s,$(wildcard demo/*.c)) \
        demo/tiny_c.s demo/segdemo_c.s
clean-demos:
	rm -f $(DEMOS) demo/*.o $(GEN_S) demo/*.map

	rm -f core/*.o core/ui/*.o core/xemu/*.o sdl/*.o core/opl2/*.o test/fstest test/seqtest test/romtest rom/kernal.bin rom/kernal.s rom/*.o rom/kernal.map test/cputest test/woztest test/maptest test/dmatest test/vickytest test/capture rom/demo.bin sdl/k4510 k4510 rom/wozmon.bin

.PHONY: all test clean rom

# Mad Pascal programs (pascal/README.md): mp from a checkout the K4510 target was installed into
MP_DIR ?= $(HOME)/Projects/neo6502_dev/Mad-Pascal
MADS   ?= $(HOME)/Projects/neo6502_dev/Mad-Assembler/mads
PAS_PRGS = $(patsubst demo/pas/%.pas,fs/PRG/%.prg,$(wildcard demo/pas/*.pas))
pascal-prgs: $(PAS_PRGS)
pascal: pascal-prgs
pascal-install:
	python3 pascal/install.py $(MP_DIR)
fs/PRG/%.prg: demo/pas/%.pas $(wildcard pascal/mp/base/k4510/*) $(wildcard pascal/mp/lib/*)
	cd demo/pas && $(MP_DIR)/bin/mp $*.pas -target:k4510 -o:$*.a65 >/dev/null
	$(MADS) demo/pas/$*.a65 -x -i:$(MP_DIR)/base -o:$@ >/dev/null

# Demo programs: C with cc65, .prg files (4-byte header) loaded by the ROM
DEMOS = fs/PRG/balls.prg fs/PRG/cube.prg fs/PRG/mandel.prg fs/PRG/keytest.prg fs/PRG/sieve.prg fs/PRG/chrout.prg fs/PRG/segdemo.prg fs/PRG/opl2.prg fs/PRG/say.prg fs/PRG/telnet.prg fs/PRG/edit.prg fs/PRG/vi.prg fs/PRG/logo.prg fs/PRG/bug.prg fs/PRG/bench.prg fs/PRG/setup.prg fs/PRG/kommander.prg fs/PRG/ranger.prg fs/PRG/tiny.prg fs/PRG/lode.prg fs/PRG/bomber.prg fs/PRG/ansidemo.prg fs/PRG/petscii.prg fs/PRG/bands.prg fs/PRG/oplplay.prg fs/PRG/padtest.prg fs/PRG/mousetest.prg fs/PRG/skyfire.prg fs/PRG/fluffy.prg fs/PRG/chess.prg
# bomber: the Bomb Party sheet (CC-BY 3.0, data/bombparty/) as arena tiles and
# sprites; tools/mkbomber.py crops, composites and palettizes into bomber.h
demo/bomber.h: tools/mkbomber.py data/bombparty/bomb_party_v4.png
	python3 tools/mkbomber.py >/dev/null
fs/PRG/bomber.prg: demo/bomber.c demo/bomber.h demo/far.h demo/k4510.h demo/prg0.o demo/romcalls.o demo/prg.cfg
	cc65 -O -t none --cpu 65c02 -o demo/bomber.s demo/bomber.c
	ca65 --cpu 65c02 -o demo/bomber.o demo/bomber.s
	ld65 -C demo/prg.cfg -o $@ demo/prg0.o demo/romcalls.o demo/bomber.o none.lib -m demo/bomber.map
demo/prg0.o: demo/prg0.s
	ca65 --cpu 65c02 -o $@ $<
demo/romcalls.o: demo/romcalls.s
	ca65 --cpu 65c02 -o $@ $<
fs/PRG/%.prg: demo/%.c demo/k4510.h demo/far.h demo/prg0.o demo/romcalls.o demo/prg.cfg
	cc65 -O -t none --cpu 65c02 -o demo/$*.s demo/$*.c
	ca65 --cpu 65c02 -o demo/$*.o demo/$*.s
	ld65 -C demo/prg.cfg -o $@ demo/prg0.o demo/romcalls.o demo/$*.o none.lib -m demo/$*.map
# EhBASIC 2.22 as a .prg at $7000 (basic/: Lee Davison's basic.asm + K4510 glue)
# segmented program (K-03): own header + linker config, overlays at 000
# tiny: Kenney's Tiny Dungeon (CC0, data/tinydungeon/) as a scrolling tile map
# with sprites; tools/mktiny.py makes the tiles, the map and tiny.h from the
# sheet and the Tiled sample map, and the K4SG header carries them
demo/tiny.bin demo/tiny.h: tools/mktiny.py data/tinydungeon/tilemap_packed.png data/tinydungeon/sampleMap.tmx
	python3 tools/mktiny.py >/dev/null
fs/PRG/tiny.prg: demo/tiny.c demo/tiny.h demo/tiny.bin demo/tiny-header.s demo/far.h demo/k4510.h demo/prg0.o demo/romcalls.o demo/tiny.cfg
	cc65 -O -t none --cpu 65c02 -o demo/tiny.s.tmp demo/tiny.c && mv demo/tiny.s.tmp demo/tiny_c.s
	ca65 --cpu 65c02 -o demo/tiny_c.o demo/tiny_c.s
	ca65 --cpu 65c02 -o demo/tiny_h.o demo/tiny-header.s
	ld65 -C demo/tiny.cfg -o $@ demo/prg0.o demo/romcalls.o demo/tiny_c.o demo/tiny_h.o none.lib -m demo/tiny.map
# skyfire: a Galaxian with Kenney's Pixel Shmup planes (CC0, data/pixelshmup/);
# tools/mkskyfire.py cuts the sheets and lays the ground, the K4SG header carries them
demo/skyfire.bin demo/skyfire.h: tools/mkskyfire.py data/pixelshmup/ships_packed.png data/pixelshmup/tiles_packed.png
	python3 tools/mkskyfire.py >/dev/null
fs/PRG/skyfire.prg: demo/skyfire.c demo/skyfire.h demo/skyfire.bin demo/skyfire-header.s demo/far.h demo/k4510.h demo/prg0.o demo/romcalls.o demo/skyfire.cfg
	cc65 -O -t none --cpu 65c02 -o demo/skyfire.s.tmp demo/skyfire.c && mv demo/skyfire.s.tmp demo/skyfire_c.s
	ca65 --cpu 65c02 -o demo/skyfire_c.o demo/skyfire_c.s
	ca65 --cpu 65c02 -o demo/skyfire_h.o demo/skyfire-header.s
	ld65 -C demo/skyfire.cfg -o $@ demo/prg0.o demo/romcalls.o demo/skyfire_c.o demo/skyfire_h.o none.lib -m demo/skyfire.map
# chess: the KoboChess drawn pieces (Doc's own, data/chess/) as 4 bpp sprites;
# tools/mkchess.py sorts ink from paper so a palette bank per side colours them
demo/chess.bin demo/chess.h: tools/mkchess.py $(wildcard data/chess/*.png)
	python3 tools/mkchess.py >/dev/null
fs/PRG/chess.prg: demo/chess.c demo/chess.h demo/chess.bin demo/chess-header.s demo/far.h demo/k4510.h demo/prg0.o demo/romcalls.o demo/chess.cfg
	cc65 -O -t none --cpu 65c02 -o demo/chess.s.tmp demo/chess.c && mv demo/chess.s.tmp demo/chess_c.s
	ca65 --cpu 65c02 -o demo/chess_c.o demo/chess_c.s
	ca65 --cpu 65c02 -o demo/chess_h.o demo/chess-header.s
	ld65 -C demo/chess.cfg -o $@ demo/prg0.o demo/romcalls.o demo/chess_c.o demo/chess_h.o none.lib -m demo/chess.map
# fluffy: a platformer with the Game Boy tileset and Fluffy (CC0, data/gbplatformer/);
# tools/mkfluffy.py cuts the tiles and the hero, draws the enemy and lays the level
demo/fluffy.bin demo/fluffy.h: tools/mkfluffy.py data/gbplatformer/gameboy_tileset.png data/gbplatformer/IdleAndWalk_strip5.png
	python3 tools/mkfluffy.py >/dev/null
fs/PRG/fluffy.prg: demo/fluffy.c demo/fluffy.h demo/fluffy.bin demo/fluffy-header.s demo/far.h demo/k4510.h demo/prg0.o demo/romcalls.o demo/fluffy.cfg
	cc65 -O -t none --cpu 65c02 -o demo/fluffy.s.tmp demo/fluffy.c && mv demo/fluffy.s.tmp demo/fluffy_c.s
	ca65 --cpu 65c02 -o demo/fluffy_c.o demo/fluffy_c.s
	ca65 --cpu 65c02 -o demo/fluffy_h.o demo/fluffy-header.s
	ld65 -C demo/fluffy.cfg -o $@ demo/prg0.o demo/romcalls.o demo/fluffy_c.o demo/fluffy_h.o none.lib -m demo/fluffy.map
fs/PRG/segdemo.prg: demo/segdemo.c demo/segdemo-header.s demo/far.h demo/k4510.h demo/prg0.o demo/romcalls.o demo/seg.cfg
	cc65 -O -t none --cpu 65c02 -o demo/segdemo.s.tmp demo/segdemo.c && mv demo/segdemo.s.tmp demo/segdemo_c.s
	ca65 --cpu 65c02 -o demo/segdemo_c.o demo/segdemo_c.s
	ca65 --cpu 65c02 -o demo/segdemo_h.o demo/segdemo-header.s
	ld65 -C demo/seg.cfg -o $@ demo/prg0.o demo/romcalls.o demo/segdemo_c.o demo/segdemo_h.o none.lib -m demo/segdemo.map

# Microsoft BASIC for 6502 as a .prg at $7000 (msbasic/: mist64's ca65
# reconstruction of Microsoft's MIT source release, vendored unmodified --
# only the files a pure-MS configuration assembles; basic/k4510msbasic.asm
# is the whole K4510 port: config, console glue, .prg header)
fs/MSBASIC/msbasic.prg: basic/k4510msbasic.asm basic/msbasic.cfg $(wildcard basic/msbasic/*.s)
	ca65 -I basic/msbasic -o basic/k4510msbasic.o basic/k4510msbasic.asm
	ld65 -C basic/msbasic.cfg -o $@ basic/k4510msbasic.o

fs/EHBASIC/ehbasic.prg: basic/k4510basic.asm basic/k4510gfx.asm basic/k4510file.asm basic/k4510math.asm basic/k4510expr.asm basic/basic.asm basic/basic.cfg
	ca65 -g --cpu 65c02 --feature labels_without_colons -o basic/k4510basic.o basic/k4510basic.asm
	ld65 -C basic/basic.cfg -o $@ basic/k4510basic.o
# Tali Forth 2 (public domain, vendored unmodified in forth/tali/) as a .prg
# loaded at $4000; forth/platform.asm is the whole port (I/O + memory map)
fs/FORTH/forth.prg: forth/platform.asm forth/tali/taliforth.asm forth/tali/definitions.asm forth/tali/stringtable.asm forth/tali/forth_words.asc $(wildcard forth/tali/words/*.asm)
	64tass --nostart -q forth/platform.asm -o $@

# RunCPM (MIT, vendored unmodified in cpm/src/) -- the Z80 second processor:
# CP/M 2.2 on the Tube, internal CCP (no DRI binaries), drives in fs/CPM/
# The in-process Tube (what the Pi runs on core 3), built on the desktop
# with the interpreter on a thread so it can be tested here first:
#   make tubetest   -> test/tubetest, then the BBC BASIC round trip
TUBE_IP_CFLAGS = -DK4510_TUBE -DK4510_TUBE_INPROC -Icore -Itube/include -Wno-array-bounds -Wno-unused-result \
                 -ffast-math -fno-finite-math-only
TUBE_IP_DEPS = $(wildcard tube/include/*.h) core/tube_cp.h
TUBE_IP_OBJS = tube/ip_bbmain.o tube/ip_bbexec.o tube/ip_bbeval.o tube/ip_bbasmb.o tube/ip_bbdata.o tube/ip_bbccos.o tube/ip_bbccon.o
CORE_IP_OBJS = $(filter-out core/io.o,$(CORE_OBJS)) core/io_ip.o core/tube_cp.o cpm/ip_runcpm.o
cpm/ip_runcpm.o: cpm/src/main.c $(wildcard cpm/src/*.h) core/tube_cp.h
	$(CC) -O2 -Wall -Wno-unused-variable -Wno-unused-function -Icore -DK4510_TUBE -Dmain=tube_cpm_main -DCCP_INTERNAL -DCPU=\"cpu1.h\" -c -o $@ $<
tube/ip_bbmain.o: tube/src/bbmain.c $(TUBE_IP_DEPS)
	$(CC) $(CFLAGS) $(TUBE_IP_CFLAGS) -c -o $@ $<
tube/ip_bbexec.o: tube/src/bbexec.c $(TUBE_IP_DEPS)
	$(CC) $(CFLAGS) $(TUBE_IP_CFLAGS) -c -o $@ $<
tube/ip_bbeval.o: tube/src/bbeval.c $(TUBE_IP_DEPS)
	$(CC) $(CFLAGS) $(TUBE_IP_CFLAGS) -fmath-errno -c -o $@ $<
tube/ip_bbasmb.o: tube/src/bbasmb_x86_64.c $(TUBE_IP_DEPS)
	$(CC) $(CFLAGS) $(TUBE_IP_CFLAGS) -Os -c -o $@ $<
tube/ip_bbccos.o: tube/src/bbccos.c $(TUBE_IP_DEPS)
	$(CC) $(CFLAGS) $(TUBE_IP_CFLAGS) -Os -c -o $@ $<
tube/ip_bbccon.o: tube/src/bbccon.c $(TUBE_IP_DEPS)
	$(CC) $(CFLAGS) $(TUBE_IP_CFLAGS) -Os -c -o $@ $<
tube/ip_bbdata.o: tube/src/bbdata_x86_64.nas
	@if command -v nasm >/dev/null; then nasm -f elf64 -s $< -o $@; else echo "no nasm: reusing tube/bbdata.o"; cp tube/bbdata.o $@; fi
core/io_ip.o: core/io.c core/io.h core/tube_cp.h core/net.h
	$(CC) $(CFLAGS) -DK4510_TUBE_INPROC -c -o $@ $<
core/tube_cp.o: core/tube_cp.c core/tube_cp.h
test/tubetest: test/headless.c $(CORE_IP_OBJS) $(TUBE_IP_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lpthread
tubetest: test/tubetest rom/kernal.bin
	./test/tubetest.sh

cpm/runcpm: cpm/src/main.c $(wildcard cpm/src/*.h)
	cc -Wall -O2 -Wno-unused-variable -DCCP_INTERNAL -DCPU=\"cpu1.h\" cpm/src/main.c -o $@

demos: $(DEMOS) fs/EHBASIC/ehbasic.prg fs/MSBASIC/msbasic.prg fs/FORTH/forth.prg
.PHONY: demos
