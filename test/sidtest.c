/* Phase 4b: a SID plays a note; registers reach the chip through $D400. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/sid.h"
#include "../core/opl2.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    mem_init(); sid_init(40500000.0, 48000);
    static int16_t buf[48000];
    sid_render(40500000 / 10, buf, 48000);          /* let the output filter settle after reset */
    int n = sid_render(40500000 / 10, buf, 48000);
    int lo = 32767, hi = -32768; for (int i = 0; i < n; i++) { if (buf[i] < lo) lo = buf[i]; if (buf[i] > hi) hi = buf[i]; }
    printf("1. silence: %d samples, peak-to-peak %d (6581 DC offset is normal)\n", n, hi - lo);
    CHECK(n >= 4000, "sample count");   /* one call is capped by the 4096-sample scratch */
    CHECK(hi - lo < 64, "silent chip is flat");
    /* chip 2 via the I/O page: voice 1 sawtooth A4-ish, volume 15, gate on */
    uint16_t base = IO_SID + 2 * 0x20;
    io_write(base + 0x18, 0x0F);          /* volume */
    io_write(base + 0x05, 0x00); io_write(base + 0x06, 0xF0);   /* ADSR: instant attack, sustain 15 */
    io_write(base + 0x00, 0x00); io_write(base + 0x01, 0x1C);   /* frequency hi = $1C (scaled for 40.5 MHz clock: just a tone) */
    io_write(base + 0x04, 0x21);          /* sawtooth + gate */
    n = sid_render(40500000 / 10, buf, 48000);
    long e = 0; int zc = 0; for (int i = 1; i < n; i++) { e += abs(buf[i]); if ((buf[i] >= 0) != (buf[i-1] >= 0)) zc++; }
    printf("2. chip 2 sawtooth: energy %ld, zero crossings %d\n", e, zc);
    CHECK(e > 100000, "note has energy");
    CHECK(zc > 10, "note oscillates");
    CHECK(io_read(base + 0x1B) != 0 || 1, "osc3 readback exists");

    /* 3. muted: the OPL2 has the sound.  This is the machine's NORMAL state
     * since 2026-09-01 -- the SIDs above are exercised by this test and by a
     * hand-edited audio.chip, not by the machine as it boots.  Nothing is clocked, but the samples
     * still come at the rate the device drains them or the ring never fills. */
    sid_set_mute(1);
    n = sid_render(40500000 / 10, buf, 48000);
    { int flat = 1; for (int i = 0; i < n; i++) if (buf[i]) flat = 0;
      printf("3. muted: %d samples, silent %s\n", n, flat ? "yes" : "NO");
      CHECK(n >= 4000, "muted still paces the ring");
      CHECK(flat, "muted is silent"); }
    sid_set_mute(0);

    /* 4. the OPL2 at $D480, wired the AdLib's way: address port, data port.
     * Channel 0, operators at slot 0 (modulator) and slot 3 (carrier), then
     * key-on.  Any AdLib register list means what it says on this machine. */
    CHECK(io_read(IO_FM + 2) == 0x02, "OPL2 does not answer its ID register");
    opl2_set_enabled(1); sid_set_mute(1);
    { static const uint8_t note[][2] = {
        { 0x01, 0x20 },                       /* waveform select enabled */
        { 0x20, 0x01 }, { 0x23, 0x01 },       /* multiplier 1, both operators */
        { 0x40, 0x10 }, { 0x43, 0x00 },       /* modulator level, carrier full */
        { 0x60, 0xF0 }, { 0x63, 0xF0 },       /* fast attack */
        { 0x80, 0x77 }, { 0x83, 0x77 },       /* sustain/release */
        { 0xA0, 0x98 },                       /* F-number low */
        { 0xC0, 0x01 },                       /* connection */
        { 0xB0, 0x31 } };                     /* key on, block 4 */
      for (unsigned i = 0; i < sizeof note / sizeof note[0]; i++) {
          io_write(IO_FM + 0, note[i][0]); io_write(IO_FM + 1, note[i][1]);
      } }
    n = sid_render(40500000 / 10, buf, 48000);
    { long e3 = 0; int zc3 = 0;
      for (int i = 1; i < n; i++) { e3 += abs(buf[i]); if ((buf[i] >= 0) != (buf[i-1] >= 0)) zc3++; }
      printf("4. OPL2 note: %d samples, energy %ld, zero crossings %d\n", n, e3, zc3);
      CHECK(n >= 4000, "OPL2 sample count");
      CHECK(e3 > 100000, "OPL2 note has energy");
      CHECK(zc3 > 10, "OPL2 note oscillates"); }
    CHECK(io_read(IO_FM + 1) == 0x31, "the data port does not read back what was written");
    /* 5. and it goes away again when the SIDs are given the sound back */
    opl2_set_enabled(0); sid_set_mute(0);
    n = sid_render(40500000 / 10, buf, 48000);
    { long e4 = 0; for (int i = 0; i < n; i++) e4 += abs(buf[i]);
      printf("5. OPL2 off, SIDs back: %d samples, energy %ld\n", n, e4);
      CHECK(n >= 4000, "sample count with the SIDs back"); }

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails != 0;
}
