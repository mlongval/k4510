#
# BMC-K4510 on a Raspberry Pi 3B+, bare metal: Circle + circle-libsdl2.
# The frontend is sdl/main.c, unchanged; this directory supplies the Circle
# kernel (three cores), the host glue, and the C64 keyboard on GPIO. Files
# opened from the emulator core go through the library's I/O service
# (circle-syscallwrap redirects the C library's open/read/opendir/...).
#
#   make BOARD=rpi3 SHIM=/path/to/circle-libsdl2
#
BOARD ?= rpi3
SHIM  ?= $(HOME)/Projects/k4510-pi/circle-libsdl2
CIRCLESTDLIBHOME ?= $(SHIM)/circle-stdlib-$(BOARD)

include $(CIRCLESTDLIBHOME)/Config.mk

CORE = ../core
TUBE = ../tube/src
RESID_SRC = $(wildcard $(CORE)/resid/*.cc)
OBJS = main.o kernel.o host_circle.o c64kbd.o frontend.o tube_pi.o net_pi.o \
       $(CORE)/xemu/cpu65.o $(CORE)/mem.o $(CORE)/io.o $(CORE)/vicky.o $(CORE)/sid.o $(CORE)/tube_cp.o $(CORE)/net.o $(CORE)/term.o $(CORE)/state.o $(CORE)/hostid.o $(CORE)/ui/settings.o $(CORE)/ui/menu.o $(CORE)/ui/ui_draw.o \
       $(CORE)/opl2.o $(CORE)/opl2/fmopl.o $(CORE)/vice_clk.o $(CORE)/sidq.o \
       bb_main.o bb_exec.o bb_eval.o bb_asmb.o bb_data.o bb_ccos.o bb_ccon.o runcpm.o \
       $(RESID_SRC:.cc=.o)

STANDARD = -std=c++23 -Wno-volatile
C_STANDARD = -std=gnu11

# core/io.c includes core/build.h, which carries the commit this build came
# from.  The desktop Makefile generates it; this build has to as well, or the
# Pi cannot compile at all -- and a Pi kernel should report its own commit,
# not whichever machine last ran the desktop build.
K4510_BUILD := 0.5-$(shell cd $(CORE)/.. && git rev-parse --short=7 HEAD 2>/dev/null || echo nogit)$(shell cd $(CORE)/.. && git diff --quiet HEAD 2>/dev/null || echo +)
.PHONY: FORCE
FORCE:
$(CORE)/build.h: FORCE
	@printf '/* generated: the commit this build came from */\n#define K4510_BUILD "%s"\n' '$(K4510_BUILD)' > $@.tmp; \
	 cmp -s $@.tmp $@ 2>/dev/null || mv $@.tmp $@; rm -f $@.tmp
$(CORE)/io.o: $(CORE)/build.h
frontend.o: $(CORE)/build.h      # sdl/main.c stamps PERF.TXT with it too

# Circle's Rules.mk says `OPTIMIZE ?= -O2`, so this wins.  The emulator is a
# callback-per-byte interpreter on an in-order core; -O3's inlining and
# unrolling are worth having here even if they cost image size.
OPTIMIZE = -O3 -fomit-frame-pointer
include $(SHIM)/circle-syscallwrap/syscallwrap.mk
include $(CIRCLEHOME)/Rules.mk

INCLUDE := -I $(SHIM)/include $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE) -I $(CORE)
CFLAGS  += -DK4510_PI -DK4510_TUBE_INPROC -Wno-unused-function
LIBS := $(SHIM)/libSDL2-$(BOARD).a $(CIRCLE_STDLIB_LIBS)

# The two vendored pieces of VICE.  They are plain C and want their own
# directory on the include path, because the shim headers there stand in for
# the emulator they came from -- and that directory must come FIRST, or
# Circle's own headers of the same names would be found instead.
$(CORE)/opl2/fmopl.o: $(CORE)/opl2/fmopl.c
	@echo "  CC    $@"
	@$(CC) -I $(CORE)/opl2 $(CFLAGS) $(C_STANDARD) -Wno-unused-parameter -c -o $@ $<

# the desktop frontend, compiled here under another name
frontend.o: ../sdl/main.c
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) $(C_STANDARD) -I $(SHIM)/include/SDL2 -c -o $@ $<

# The Tube co-processor: Richard Russell's BBC BASIC (tube/src, see
# tube/ALTERED.md) compiled into the kernel with -DK4510_TUBE, the flags
# its own Makefile uses, and the AArch64 data/assembler files.
# -fPIC: the data file lays its variables out byte-packed, and non-PIC
# AArch64 code reaches a global with adrp + ldr :lo12:, which needs the
# symbol aligned to the access -- the Linux build is PIC and goes through
# the GOT, so do we. -Dpfree: BBC's heap pointer shares a name with
# Circle's free(); renamed in C here and in the data object below.
BBCDEPS = $(wildcard ../tube/include/*.h) $(CORE)/tube_cp.h
BBCFLAGS = $(CFLAGS) $(C_STANDARD) -DK4510_TUBE -I ../tube/include -Wno-array-bounds -Wno-unused-result -Wno-unused-variable \
           -ffast-math -fno-finite-math-only -fPIC -Dpfree=bbc_pfree
bb_main.o: $(TUBE)/bbmain.c $(BBCDEPS)
	@echo "  CC    $@"
	@$(CC) $(BBCFLAGS) -O2 -c -o $@ $<
bb_exec.o: $(TUBE)/bbexec.c $(BBCDEPS)
	@echo "  CC    $@"
	@$(CC) $(BBCFLAGS) -O2 -c -o $@ $<
bb_eval.o: $(TUBE)/bbeval.c $(BBCDEPS)
	@echo "  CC    $@"
	@$(CC) $(BBCFLAGS) -O2 -fmath-errno -c -o $@ $<
bb_asmb.o: $(TUBE)/bbasmb_arm_64.c $(BBCDEPS)
	@echo "  CC    $@"
	@$(CC) $(BBCFLAGS) -Os -c -o $@ $<
bb_ccos.o: $(TUBE)/bbccos.c $(BBCDEPS)
	@echo "  CC    $@"
	@$(CC) $(BBCFLAGS) -Os -c -o $@ $<
bb_ccon.o: $(TUBE)/bbccon.c $(BBCDEPS)
	@echo "  CC    $@"
	@$(CC) $(BBCFLAGS) -Os -c -o $@ $<
# bbdata_arm_64.s names its globals with a leading underscore (the Apple
# convention); ELF has none, so C's `liston` never meets its `_liston`.
# The link is repaired mechanically: every global (D/T/U) loses the
# underscore, locals such as b4fmt are untouched.
# RunCPM (cpm/src, one translation unit) as Tube program 3
runcpm.o: ../cpm/src/main.c $(wildcard ../cpm/src/*.h) $(CORE)/tube_cp.h
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) $(C_STANDARD) -O2 -Wno-unused-variable -DK4510_TUBE -Dmain=tube_cpm_main -DCCP_INTERNAL -DCPU=\"cpu1.h\" -c -o $@ $<

bb_data.o: $(TUBE)/bbdata_arm_64.s
	@echo "  AS    $@"
	@$(CC) $(AFLAGS) -c -o $@.raw $<
	@$(PREFIX)nm $@.raw | awk '$$NF ~ /^_/ && $$(NF-1) ~ /^[DTU]$$/ { print $$NF, ($$NF == "_pfree" ? "bbc_pfree" : substr($$NF, 2)) }' > bb_data.syms
	@$(PREFIX)objcopy --redefine-syms=bb_data.syms $@.raw $@

# reSID is .cc; Circle's rules only know .cpp
%.o: %.cc
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) -DVERSION=\"0.16\" -Wno-unused-parameter -c -o $@ $<

-include $(DEPS)

include $(SHIM)/sdl-app.mk

# syscallwrap.mk comes before Rules.mk and would otherwise be the default goal
.DEFAULT_GOAL := kernel8.img
