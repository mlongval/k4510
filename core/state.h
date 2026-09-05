/* Save states: the whole machine to a file and back -- the CPU, every
 * non-zero 4 KB page of the 256 MB, the MAP and bank registers, the far
 * gate, VICKY, the devices in the I/O page (keyboard queue, filesystem
 * cwd, DMA, MATH, SYS, the sequencer, the Tube ULA) and JIM.  The OPL2's
 * registers are not carried: a state loads with the chip reset. Not saved: the Tube co-processor
 * (a program on another core; a state is loaded with the Tube stopped)
 * and network connections. The file is chunked (tag, length, bytes);
 * a chunk of another size than this build expects fails the load, so
 * a state from another version is refused rather than misread. */
#ifndef K4510_STATE_H
#define K4510_STATE_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int  state_save(const char *path);          /* 0 ok, -1 cannot write */
int  state_load(const char *path);          /* 0 ok, -1 no file, -2 not a state / another version */
/* for the modules' hooks */
int  state_put(FILE *f, const char *tag, const void *p, size_t n);
int  state_get(FILE *f, const char *tag, void *p, size_t n);   /* 0 ok; -2 tag or size differ */
void mem_state_save(FILE *f);   int mem_state_load(FILE *f);
void vicky_state_save(FILE *f); int vicky_state_load(FILE *f);
void io_state_save(FILE *f);    int io_state_load(FILE *f);
void term_state_save(FILE *f);  int term_state_load(FILE *f);
#ifdef __cplusplus
}
#endif
#endif
