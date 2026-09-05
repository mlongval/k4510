//
// BMC-K4510 on the Pi 3B+, bare metal, on three cores:
//   core 0  Circle's world -- USB, the SD card, sound, the library's servo
//   core 1  the emulator: sdl/main.c unchanged, calling plain SDL_*
//   core 2  presentation: the library scales each finished frame to the glass
//   core 3  the Tube co-processor: BBC BASIC, compiled in, waiting at $D803
// Files live in SD:/k4510/{rom,data,fs}; the emulator's fopen/opendir from
// core 1 are redirected to the library's I/O service by circle-syscallwrap
// (see Makefile), so core/io.c is the same file as on the desktop.
//
#include "kernel.h"
#include <circle/sysconfig.h>   /* DEFAULT_KEYMAP, for the boot report */
#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_circle.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>

extern "C" int  k4510_frontend_main(int argc, char **argv);
extern "C" void c64kbd_init(void);
extern "C" void tube_cp_run(void);

static const char From[] = "k4510";
static std::atomic<int> s_AppGate{0};
static CLogger *s_Logger;

static inline void PublishToOtherCores(void) { asm volatile("dsb ish; sev" ::: "memory"); }
static void ParkCore(void) { for (;;) asm volatile("wfe" ::: "memory"); }

void CK4510Cores::Run(unsigned nCore)
{
    SDL2Circle_ArmCoreRuntime();
    switch (nCore) {
    case 1: {
        while (!s_AppGate.load(std::memory_order_acquire)) asm volatile("wfe" ::: "memory");
        static char a0[] = "k4510", a1[] = "rom/kernal.bin", a2[] = "fs";
        char *argv[] = { a0, a1, a2, nullptr };
        int rc = k4510_frontend_main(3, argv);
        // Doc: on the Pi, "Quit" should just power the machine down. There is
        // no operating system to return to; a reboot only put the banner back.
        SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE, "emulator returned %d; powering off", rc);
        SDL2Circle_CallOn0([](void *) { halt(); }, nullptr);
        ParkCore();
        break; }
    case 2:
        SDL2Circle_SplitPresentCore();          // never returns
        break;
    case 3:
        while (!s_AppGate.load(std::memory_order_acquire)) asm volatile("wfe" ::: "memory");
        tube_cp_run();                          // never returns: the second processor idles until the ROM starts it
        break;
    default:
        ParkCore();
        break;
    }
}

CKernel::CKernel(void)
    : m_Serial(0, FALSE, 0), m_Timer(&m_Interrupt), m_Logger(m_Options.GetLogLevel(), &m_Timer),
      m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED)
{
    SDL2Circle_HardwareInit();      // CPU clock first: the SD host's timing derives from the core clock
    m_ActLED.Blink(2);
}

boolean CKernel::Initialize(void)
{
    boolean bOK = TRUE;
    if (bOK) bOK = m_Serial.Initialize(115200);
    if (bOK) bOK = m_Logger.Initialize(&m_Serial);
    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    if (bOK) bOK = m_EMMC.Initialize();
    if (bOK) { FRESULT r = f_mount(&m_FileSystem, "SD:", 1); if (r != FR_OK) { m_Logger.Write(From, LogError, "mount SD: failed (%d)", (int)r); bOK = FALSE; } }
    if (bOK) SDL2Circle_ArmCoreRuntime();
    if (bOK) bOK = m_Cores.Initialize();        // last: the secondary cores park until Run() opens the gate
    s_Logger = &m_Logger;
    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(From, LogNotice, "BMC-K4510 -- 45GS02 / VICKY / SHEILA / OPL2; core 0 devices, core 1 emulator, core 2 presentation, core 3 the Tube");
    if (SDL2Circle_DeclareVirtualDevice(32, 640, 480) != 0)
        m_Logger.Write(From, LogWarning, "virtual device: %s", SDL_GetError());
    int ok = 0;
    for (int i = 0; i < 5 && !ok; i++) { if (chdir("SD:/k4510") == 0) ok = 1; else m_Timer.MsDelay(200); }
    if (!ok) { m_Logger.Write(From, LogError, "no SD:/k4510 directory on the card"); return ShutdownHalt; }

    /* ---- what the keyboard layout actually resolved to ---------------------
     * Two attempts at making the USB keyboard American both failed and both
     * looked identical from here -- the first because cmdline.txt was missing,
     * the second because it ended in a newline and Circle splits its command
     * line on spaces alone, so "us\n" matched no layout and it fell back to
     * DEFAULT_KEYMAP ("DE").  Guessing a third time is not a plan.
     *
     * So the machine says what it got.  Written where Doc can read it without
     * a serial cable: TYPE /SYSTEM/BOOT.TXT at the shell.  It records the raw
     * command line the firmware passed, what CKernelOptions parsed out of it,
     * and what the compiled-in fallback is -- which between them say whether
     * the file arrived, whether it parsed, or whether something downstream is
     * ignoring the answer. */
    {
        const char *km = m_Options.GetKeyMap();
        FILE *f = fopen("SD:/k4510/fs/SYSTEM/BOOT.TXT", "w");
        if (f) {
            fprintf(f, "K4510 boot report\n=================\n\n");
            fprintf(f, "keymap= from cmdline.txt : \"%s\"%s\n",
                    km ? km : "(null)",
                    (km && *km) ? "" : "   <-- EMPTY: the option did not arrive or did not parse");
            fprintf(f, "compiled-in fallback     : \"%s\"\n", DEFAULT_KEYMAP);
            fprintf(f, "\nIf the first line is empty, Circle used the fallback and the\n");
            fprintf(f, "keyboard is whatever that says.  cmdline.txt must sit in the root\n");
            fprintf(f, "of the card, contain keymap=us, and end WITHOUT a newline.\n");
            fclose(f);
        }
        m_Logger.Write(From, LogNotice, "keymap option \"%s\", fallback \"%s\"",
                       km ? km : "(null)", DEFAULT_KEYMAP);
    }

    c64kbd_init();                              // GPIO pins are plain MMIO: core 1 may poll them directly
    SDL2Circle_SplitInit();
    // The network is NOT started here. pi/net_pi.cpp starts it on the machine's
    // first request: started at boot it cost the emulator core nine tenths of
    // its speed, cable or no cable.
    s_AppGate.store(1, std::memory_order_release);
    PublishToOtherCores();
    for (;;) m_Scheduler.Yield();               // core 0 gives the servo its time
}
