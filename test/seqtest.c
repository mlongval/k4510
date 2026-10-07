/* The sound sequencer ($D5E0-$D5E3) plays through the OPL2: a note queued on
 * each channel must make the chip sound, and the queue must hold a note for
 * its duration and then let it go.  Written 2026-09-05, the day the SIDs the
 * sequencer used to drive were removed. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/audio.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define CYC (40500000 / 60 / 480)
static long energy(int frames)             /* sum |sample| over n frames of rendered sound */
{
    long e = 0;
    while (frames--) {
        for (int y = 0; y < 480; y++) {
            int16_t t[256]; int n = audio_render(CYC, t, 256);
            for (int i = 0; i < n; i++) { e += t[i] < 0 ? -t[i] : t[i]; }
        }
        io_frame_tick();
    }
    return e;
}
static void note(int ch, int amp, int pitch, int dur)
{
    io_write(0xD5E0, (uint8_t)ch); io_write(0xD5E1, (uint8_t)amp); io_write(0xD5E2, (uint8_t)pitch); io_write(0xD5E3, (uint8_t)dur);
}
int main(void)
{
    io_set_opts(SYSOPT_NOBOOT);
    if (mem_init()) return 1; io_reset(); audio_init(40500000.0, 48000);
    long quiet = energy(10);
    CHECK(quiet == 0, "silent machine makes sound: %ld", quiet);
    for (int ch = 0; ch < 4; ch++) {
        io_reset();
        note(ch, -15, 100, 255);                             /* hold forever */
        long e = energy(10);
        CHECK(e > 100000, "channel %d: SOUND %d,-15,100,255 made %ld of energy", ch, ch, e);
        io_write(0xD5E0, 0x80);                              /* silence everything now */
        long after = energy(10);
        CHECK(after < e / 20, "channel %d: not silenced (%ld after %ld)", ch, after, e);
    }
    io_reset();
    note(1, -15, 100, 10);                                   /* half a second (30 frames), then release */
    long during = energy(30), gone = energy(20);
    CHECK(during > 100000 && gone < during / 20, "a timed note did not stop: %ld then %ld", during, gone);
    note(1, -15, 100, 5); note(1, -15, 120, 5);              /* two queued: the second follows the first */
    long first = energy(15), second = energy(15), none = energy(15);
    CHECK(first > 100000 && second > 100000 && none < first / 20, "queue: %ld %ld %ld", first, second, none);
    printf("%s: the sequencer sounds through the OPL2, holds, releases and queues\n", fails ? "FAIL" : "OK");
    return fails ? 1 : 0;
}
