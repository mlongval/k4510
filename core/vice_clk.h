/* The machine's microsecond clock, shared by the two pieces of VICE the
 * K4510 vendors: the OPL2 ages its last register store against it, and the
 * OPL2 sets its two timers by it.  It is NOT the CPU's clock -- the CPU's
 * rate is a setting and moves -- so a timer period means the same number of
 * microseconds whatever the machine is clocked at.  core/sid.cc advances it
 * as the sound is rendered, whichever engine is doing the rendering, and
 * whether or not the SIDs are the ones being heard. */
#ifndef K4510_VICE_CLK_H
#define K4510_VICE_CLK_H
#include <stdint.h>
#define K4510_VICE_CLK_HZ 1000000
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t maincpu_clk;          /* VICE's name for it; its CLOCK is uint32_t */
void vice_clk_advance(uint32_t us);   /* microseconds, then fire anything due */
void vice_clk_reset(void);
#ifdef __cplusplus
}
#endif
#endif
