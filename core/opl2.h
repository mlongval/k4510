/* The OPL2 (Yamaha YM3812) at $D480 -- nine FM voices, wired the AdLib's way.
 *
 *   $D480  W ADDR    the register to write next
 *          R STATUS  bit7 IRQ, bit6 timer 1 expired, bit5 timer 2 expired
 *   $D481  W DATA    write it;  R  the last value written to that register
 *   $D482  R ID      $02 = an OPL2 is fitted
 *
 * This is the machine's sound chip -- the only one, since 2026-09-05.
 */
#ifndef K4510_OPL2_H
#define K4510_OPL2_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void    opl2_init(int sample_rate);
void    opl2_reset(void);
void    opl2_write(uint8_t reg, uint8_t v);   /* reg 0 = ADDR, 1 = DATA */
void    opl2_write_reg(uint8_t reg, uint8_t v);   /* the machine's own use (the sequencer): one register, the
                                                   * address latch put back so a program's ADDR/DATA pair survives */
void    opl2_apply(uint8_t reg, uint8_t v);   /* the same write, performed on the rendering core
                                               * after the queue has carried it there (core/sndq.h) */
uint8_t opl2_read(uint8_t reg);               /* reg 0 = STATUS, 1 = data readback, 2 = ID */
int     opl2_render(int n, int16_t *out, int max);
#ifdef __cplusplus
}
#endif
#endif
