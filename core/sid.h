/* K4510 sound: four SID chips at IO_SID ($D400), $20 bytes each.
 * C wrapper over the vendored C++ reSID (core/resid/, Dag Lem, GPL2+).
 *
 * SINCE 2026-09-01 THE SIDS ARE OFF BY DEFAULT on every host: the machine
 * boots with the OPL2 sounding and sid_set_mute(1) in force.  Nothing here is
 * deleted -- the chips are still emulated, still register-accurate, still
 * tested (test/sidtest) -- and audio.chip in k4510.cfg still selects them.
 * They are simply not what the machine offers.  Doc, 2026-09-01: on the Pi
 * "both reSid and FastSid sound terrible.  OPL2 however sounds really nice",
 * and rather than have one machine on each host, both hosts got the OPL2.
 *
 * FastSID (VICE's wavetable engine) WAS a second engine here and was cut on
 * 2026-09-01: it existed to save frame time on a host that no longer runs
 * SIDs at all, and it was the one Doc liked least.  git has it.
 */
#ifndef K4510_SID_H
#define K4510_SID_H
#include <stdint.h>
#define K4510_SIDS 4
#ifdef __cplusplus
extern "C" {
#endif
void sid_set_max(int n);      /* clock/mix only the first n chips (audio.sids) */
void sid_set_mute(int mute);  /* the OPL2 has the sound: clock nothing, render in their place.
                                 On by default -- see the note at the top of this file. */
void sid_drain_to(uint32_t us);  /* the rendering core: perform every queued write due by then (core/sidq.h) */
#ifdef __cplusplus
}
#endif
void    sid_init(double cpu_hz, int sample_rate);
void    sid_set_clock(int sel);     /* 0 = 1 MHz (default), 1 = PAL C64 (985248), 2 = NTSC (1022730) */
void    sid_reset(void);
void    sid_set_cpu_hz(double hz);   /* the CPU clock changed; the SID clock did not */
void    sid_write(int chip, uint8_t reg, uint8_t v);
uint8_t sid_read(int chip, uint8_t reg);
void    sid_set_model(int chip, int mos8580);
/* Advance all chips by cpu cycles and mix their output into out[] (mono, 16-bit);
 * returns samples produced (<= max). Call from the audio side with the cycles elapsed. */
int     sid_render(int cycles, int16_t *out, int max);
#endif
