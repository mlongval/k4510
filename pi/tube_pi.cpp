// The Tube co-processor's platform on the Pi: time and sleep for core 3.
// SDL_GetTicks and the performance counter read the system timer directly
// from any core. The waits are deliberately NOT SDL_Delay: off core 0 the
// shim's delay runs the audio callback from the calling context (docs/
// CORE-SPLIT.md), and the sound belongs to core 1. Core 3 exists only to run
// the interpreter, so spinning on the counter with a yield hint is the
// right wait here -- exactly what the shim itself does off core 0.
#include <SDL2/SDL.h>
#include "../core/tube_cp.h"

extern "C" unsigned tube_cp_ticks(void) { return SDL_GetTicks(); }

// SDL_GetPerformanceCounter is CTimer::GetClockTicks64(), which reads the
// BCM2837 system timer -- a peripheral register on the VideoCore bus, slow to
// read and shared with everything else the SoC is doing. Reading it once per
// spin iteration meant core 3 issued something like a million MMIO reads a
// millisecond, for ever, from power-on, because this core's normal state is
// waiting for the ROM to start a co-processor that may never be started. That
// traffic is paid for by the other cores: the emulator on core 1 and the
// presentation on core 2 both go through the same bus. The Pi measured 60 fps
// on 22 August; the Tube arrived on core 3 on the 24th.
//
// So: yield in batches between reads. A batch of 1024 yields is under a
// microsecond on a 1.4 GHz A53, so the sleep is as accurate as it was, while
// the timer is read some three orders of magnitude less often.
extern "C" void tube_cp_usleep(unsigned us)
{
    Uint64 end = SDL_GetPerformanceCounter() + (Uint64)us * SDL_GetPerformanceFrequency() / 1000000u;
    while (SDL_GetPerformanceCounter() < end)
        for (int i = 0; i < 1024; i++) asm volatile("yield" ::: "memory");
}
