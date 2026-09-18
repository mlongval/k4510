# The Tube co-processor: Richard Russell's BBC BASIC (BBCTTY console
# edition, vendored, see ALTERED.md) built for the HOST the emulator runs
# on. The K4510 talks to it through the Tube device at $D800.
#   make            -> tube/bbcbasic (x86-64 host; ARM hosts use the .s data)
CC = gcc -Wall -I include
NASM ?= nasm
OBJ = bbmain.o bbexec.o bbeval.o bbasmb.o bbdata.o bbccos.o bbccon.o

# DOOM rides here too (tube/doom, see its ALTERED-K4510.md): `make -C tube'
# is what linux/build-live.sh runs, so the image gets both co-processors from
# one line.  DOOM is built without game data -- tools/get-freedoom.sh fetches
# the WAD, which is not vendored.
# ...and the Apple IIe (tube/apple, LinApple's core; its ALTERED-K4510.md).
all: bbcbasic doom apple

doom:
	$(MAKE) -C doom -f Makefile.k4510
apple:
	$(MAKE) -C apple -f Makefile.k4510

bbmain.o: src/bbmain.c include/BBC.h
	$(CC) -c -O2 -ffast-math -fno-finite-math-only $< -o $@
bbexec.o: src/bbexec.c include/BBC.h
	$(CC) -c -O2 -ffast-math -fno-finite-math-only $< -o $@
bbeval.o: src/bbeval.c include/BBC.h
	$(CC) -Wno-array-bounds -c -O2 -ffast-math -fmath-errno -fno-finite-math-only $< -o $@
bbasmb.o: src/bbasmb_x86_64.c include/BBC.h
	$(CC) -c -Os $< -o $@
bbccos.o: src/bbccos.c include/bbccon.h
	$(CC) -Wno-array-bounds -Wno-unused-result -c -Os $< -o $@
bbccon.o: src/bbccon.c include/bbccon.h
	$(CC) -Wno-array-bounds -Wno-unused-result -c -Os $< -o $@
bbdata.o: src/bbdata_x86_64.nas
	$(NASM) -f elf64 -s $< -o $@
bbcbasic: $(OBJ)
	$(CC) $(OBJ) -ldl -lm -lrt -pthread -o bbcbasic

clean:
	rm -f *.o bbcbasic
	$(MAKE) -C doom -f Makefile.k4510 clean

.PHONY: apple all doom clean
