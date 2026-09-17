/* See digimax.h. */
#include "digimax.h"
#include <stddef.h>

static uint8_t dac[4] = { 0x80, 0x80, 0x80, 0x80 };
static int rate = 48000;
static int (*stream_pull)(void);
static int stream_hz, stream_acc;

/* One DAC at full swing is +-127 * GAIN = about a fifth of the output's
 * range, so all four at once still fit and one sits comfortably over the
 * OPL2's music, which peaks around a tenth. */
#define GAIN 48

void digimax_init(int sample_rate) { rate = sample_rate > 0 ? sample_rate : 48000; digimax_reset(); }
void digimax_reset(void) { for (int i = 0; i < 4; i++) dac[i] = 0x80; stream_acc = 0; }
void digimax_write(uint8_t reg, uint8_t v) { if (reg < 4) dac[reg] = v; }
uint8_t digimax_read(uint8_t reg) { return reg < 4 ? dac[reg] : reg == 4 ? 0x04 : 0xFF; }

void digimax_stream(int (*pull)(void), int hz)
{
    if (!pull || hz <= 0) { if (stream_pull) dac[0] = 0x80; stream_pull = NULL; stream_hz = 0; stream_acc = 0; return; }
    stream_pull = pull; stream_hz = hz;
}

void digimax_mix(int16_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        if (stream_pull) {                        /* the stream's clock: hz bytes for every `rate` samples */
            stream_acc += stream_hz;
            while (stream_acc >= rate) {
                int s = stream_pull();
                stream_acc -= rate;
                dac[0] = s < 0 ? 0x80 : (uint8_t) s;   /* run dry: silence, not the last level held as DC */
            }
        }
        { int v = out[i] + ((int) dac[0] + dac[1] + dac[2] + dac[3] - 4 * 128) * GAIN;
          out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
    }
}
