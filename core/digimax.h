/* The K4510's DigiMAX -- four 8-bit DACs at $D4C0, beside the OPL2.
 *
 *   $D4C0-$D4C3  R/W  DAC 0..3   unsigned 8 bits; $80 is silence
 *   $D4C4        R    ID         $04 = four DACs are fitted
 *
 * The design has had it since the machine was drawn ("DigiMAX PCM: built-in,
 * always present", docs/K4510-Design.md A-09; "$D480-$D4FF OPL2, DigiMAX").
 * It arrived on 2026-09-17, when Doc asked for DOOM's sound effects "and the
 * engine".  It is the C64 cartridge's shape and nothing more: no FIFO, no
 * DMA, no interrupt -- whatever is in a DAC's register is what that DAC is
 * putting out, and a program makes sound by changing it quickly.
 *
 * One thing can write DAC 0 besides a program: a STREAM, which the machine
 * clocks in at a fixed rate on somebody's behalf.  That is how the Tube's
 * DOOM is heard (core/io.c hands its shared ring over as the stream), in the
 * same spirit as opl2_write_reg(): the co-processor cannot reach a register,
 * so the machine performs the write for it.
 */
#ifndef K4510_DIGIMAX_H
#define K4510_DIGIMAX_H
#include <stdint.h>

void    digimax_init(int sample_rate);
void    digimax_reset(void);
void    digimax_write(uint8_t reg, uint8_t v);   /* reg 0..3 = DAC 0..3 */
uint8_t digimax_read(uint8_t reg);               /* 0..3 = what was written; 4 = ID */
void    digimax_mix(int16_t *out, int n);        /* ADD the four DACs to n samples already holding the FM */
/* a stream into DAC 0: pull() returns the next byte or -1 for "nothing yet";
 * hz = 0 (or pull = NULL) takes it away and DAC 0 goes back to silence */
void    digimax_stream(int (*pull)(void), int hz);
#endif
