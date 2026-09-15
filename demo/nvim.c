/* K4510: NVIM -- Neovim on the Tube, set up for the machine's languages.
 *
 *   NVIM [file]      tools/k4510-nvim on the Linux, as CC runs k4510-cc
 *
 * Neovim runs on the host with tools/nvim/init.lua: the machine's colours,
 * its languages, :make (F9) with the machine's compilers and MAKE.ERR.  Its
 * :Run (F10) builds the file, leaves the command that runs it in
 * /SYSTEM/LOG/NVIM.BAT and ends with 42: this runs that here -- by SWAP -k,
 * as VI's :run does -- waits for a key, and puts Neovim back where it was.
 * Anything else Neovim ends with, this ends too (Doc, 2026-09-15: "make the
 * neovim tube and the proper setup ... and the make sequence"). */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return (unsigned char)(((unsigned (*)(void))0xFF95)() & 0xFF); }   /* as demo/ed.h: the line after the name, its address at $F0 */

static char line[128];
static void say(const char *s) { while (*s) rom_chrout((unsigned char) *s++); }

void main(void)
{
    uint8_t na = rom_args(), i, n = 0, rc; const char *a = *(const char **)0xF0; const char *s;
    for (s = "!k4510-nvim "; *s; ) line[n++] = *s++;
    while (na && *a == ' ') { a++; na--; }
    for (i = 0; i < na && n < sizeof line - 1; i++) line[n++] = a[i];
    line[n] = 0;
    for (;;) {
        rc = rom_shell(line);
        if (rc != 42) break;                          /* 42: :Run -- anything else, Neovim is done */
        rom_shell("EXEC /SYSTEM/LOG/NVIM.BAT");
        say("\n -- a key returns to Neovim -- ");
        while (!rom_getin()) ;
        say("\n");
        for (n = 0, s = "!k4510-nvim --resume"; *s; ) line[n++] = *s++;
        line[n] = 0;
    }
    if (rc == 127) say("NVIM: there is no Neovim on this Linux\n");
}
