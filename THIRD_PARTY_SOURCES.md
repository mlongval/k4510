# Third-party sources

Where every piece of code in this repository that someone else wrote came
from, precisely enough to fetch it again and check that what is here is
what they published.

`LICENSES.md` is the legal record and `CREDITS.md` is the thanks. This
file is the provenance: upstream URL, version, whether it was altered,
and how to verify it.

**Last checked: 2026-08-26** against the tree at that date. The digests
for `core/xemu`, `core/resid` (since removed) and `tube` were recomputed on that re-check:
the first two had changed since they were recorded, and `tube` had not
changed at all yet still did not reproduce, so that one was wrong when
written. The other three reproduce exactly. Every
component has a `VENDORED-FROM.txt` beside it; this file is the summary.

Digests written `dir:` are of the directory's source files, sorted by
path and concatenated -- reproduce them with the command at the bottom,
which pins `LC_ALL=C` because the sort order, and so the digest, depends
on it.
A component marked *not recorded* is a gap in this record, not a claim
that the code is unknown; those are listed again at the end.

---

## 45GS02 / 65CE02 CPU core (Xemu)

| | |
|---|---|
| Role | The CPU the machine runs on. The one component that is the real thing rather than a fantasy. |
| Local path | `core/xemu/` -- `cpu65.c`, `cpu65.h`, `cpu65_mega65_timings.h`, `cpu65ce02_disasm_tables.c`, `emutools_basicdefs.h` |
| Upstream | https://github.com/lgblgblgb/xemu (Gábor Lénárt, "LGB"). The cycle table `cpu65_mega65_timings.h` comes from a **second** repository of his, https://github.com/lgblgblgb/megacyc, which generated it. Xemu is LGB's own project, *not* a MEGA65-project repository — though it is the emulator that community uses, and what the core emulates is the 4510/45GS02 as extended in the MEGA65. |
| Version | Commit **not determined**; `cpu65.c` carries "Copyright (C)2016-2025 LGB" (`cpu65.h` says 2024), vendored 2026-08-21. `core/xemu/VENDORED-FROM.txt` says how to settle it. |
| Licence | GPL-2.0-or-later (`core/xemu/LICENSE.xemu`) |
| Altered | No. Used unchanged. |
| Verify | `dir: 8b7cdc2fcb509484` |

## reSID — REMOVED 2026-09-05

Dag Lem's reSID, as shipped in VICE 3.3, was `core/resid/` from 2026-08-22
(the machine's four SID chips at `$D400`) until 2026-09-05, when Doc had
every trace of the SIDs taken out; they had been muted since 2026-09-01.
Unaltered from upstream throughout; the git history has it (last present
at the commit before the removal, digest `dir: 69adec626fd3713d`).

## FastSID — REMOVED 2026-09-01

VICE's wavetable SID engine was vendored here on 2026-08-30 and removed on
2026-09-01, in the consolidation that made the machine an OPL2 machine.
No file of it remains in the tree (`core/fastsid/`, `core/fsid.[ch]`); the
git history has all of it, unaltered from upstream, if it is ever wanted
back.

## fmopl (the OPL2)

| | |
|---|---|
| Role | The Yamaha YM3812 at `$D480` — nine FM voices, the AdLib's chip, wired the AdLib's way. The machine's only sound chip. |
| Local path | `core/opl2/` — `fmopl.c`, `fmopl.h`. The device and wrapper `core/opl2.c` are ours. |
| Upstream | MAME's FM sound generator (Jarek Burczynski, Tatsuyuki Satoh), version 0.72, adapted for VICE by Marco van den Heuvel. Taken from **BMC64's** vendored copy — `third_party/vice-3.3/src/core/` in https://github.com/randyrossi/bmc64. |
| Version | VICE 3.3's copy, vendored 2026-08-30. `core/opl2/VENDORED-FROM.txt` |
| Licence | GPL-2.0+ (stated in the file's own header) |
| Altered | No. The other headers in that directory are K4510 shims; `alarm.h` is two timer slots and a poll where VICE has a general alarm queue. |
| Verify | `dir: 3ff3515ef70fe50f` (`fmopl.c` and `fmopl.h` only) |

## BBC BASIC (BBCSDL console edition, "BBCTTY")

| | |
|---|---|
| Role | The Tube co-processor's BASIC. |
| Local path | `tube/` (sources in `tube/src`, headers in `tube/include`) |
| Upstream | https://github.com/rtrussell/BBCSDL (Richard T. Russell) |
| Version | 1.34b (`tube/include/BBC.h`) |
| Licence | zlib. "BBC BASIC" is the name of Richard Russell's interpreter; this project holds no licence to that name and asserts no rights in it, using it only to identify what is vendored. See `tube/ALTERED.md`. |
| Altered | **Yes** -- every change marked `[K4510]`; the notice required by condition 2 of the licence is `tube/ALTERED.md`. |
| Verify | `dir: 8988e29475436852` |

## RunCPM

| | |
|---|---|
| Role | The Z80 second processor's CP/M 2.2, with the internal CCP. |
| Local path | `cpm/src/` |
| Upstream | https://github.com/MockbaTheBorg/RunCPM (Marcelo Dantas, "Mockba the Borg") |
| Version | commit `e698e8ab59c2de915b23be7f5b146a5c621f5c76`, vendored 2026-07-21 (`cpm/VENDORED-FROM.txt`) |
| Licence | MIT |
| Altered | No. Built `CCP_INTERNAL`, so no DRI binaries are distributed. |
| Verify | `dir: 607fde766f32bb56` |

## Tali Forth 2

| | |
|---|---|
| Role | The machine's Forth. |
| Local path | `forth/tali/` (the port is `forth/platform.asm`, which is ours) |
| Upstream | https://github.com/SamCoVT/TaliForth2 (Scot W. Stevenson, Sam Colwell, Patrick Surry) |
| Version | commit `cb887532b9fdc2d8c96d891dafb613ddf9640bb8`, vendored 2026-08-13 (`forth/tali/VENDORED-FROM.txt`) |
| Licence | public domain |
| Altered | No. |
| Verify | `dir: ebeb4a68bb2d232f` |

## EhBASIC 2.22

| | |
|---|---|
| Role | The machine's first BASIC. |
| Local path | `basic/basic.asm` (the K4510 glue in `basic/k4510*.asm` is ours) |
| Upstream | Lee Davison (1966--2013); the ca65 form came via https://github.com/jefftranter/6502 |
| Version | 2.22 |
| Licence | Free for non-commercial use. Derivatives must carry the string **"Derived from EhBASIC"** in any binary image, and `basic/README-EhBASIC.txt` in any human-readable distribution. Shipped as a separate program (`fs/EHBASIC/ehbasic.prg`), not linked with the GPL code. |
| Altered | **Yes** -- extended with the machine's graphics, sound, file and `*` statements. |
| Verify | `dir: df25f7f9fc1cb7ba` (whole `basic/`, ours and theirs together) |

## K4510x console font (Linux kernel VGA 8x16)

| | |
|---|---|
| Role | The K4510x Linux consoles (Ctrl+Alt+F2..F6): the IBM VGA glyphs at 24x43, so 1920x1080 is 80x25. |
| Local path | `linux/config/includes.chroot/usr/share/consolefonts/K4510-VGA24x43.psf`, generated by `data/mkconsolefont.py` |
| Upstream | the Linux kernel, `lib/fonts/font_8x16.c` |
| Version | Last changed upstream in commit `db65872b38dc` (2026-03-09); fetched from `master` 2026-09-12. `data/VENDORED-FROM-consolefont.txt` |
| Licence | GPL-2.0 |
| Altered | Scaled from the 9x16 VGA cell to 24x43 and written as PSF2 with a Unicode table; glyph shapes unchanged. |
| Verify | `sha256 fcaa9079d3f73095b1fb33125fef086af023fc0aabac0a0b36c5c4f8b4a4c8c9` |

## Keyboard layouts (XKB, via ckbcomp)

| | |
|---|---|
| Role | The machine's own keyboard layouts (F12 -> Input -> Keyboard layout), the same XKB data the K4510 Linux's consoles use. |
| Local path | `core/kbdmaps.h`, generated by `tools/mkkbdmaps.py` |
| Upstream | xkeyboard-config's symbol files, compiled by Debian console-setup's `ckbcomp` (the generator runs it); compositions from Unicode NFC (Python's `unicodedata`) |
| Version | Generated 2026-09-12 on ubuntu-s1 (Ubuntu 24.04's xkb-data and console-setup); regenerate with the command in the header. |
| Licence | xkeyboard-config: MIT/X11-style.  The generated tables are facts about key arrangements. |
| Altered | Four levels per key kept (plain, Shift, AltGr, Shift+AltGr); control and meta levels dropped. |
| Verify | `python3 tools/mkkbdmaps.py \| diff - core/kbdmaps.h` on a host with the same xkb-data |

## unscii 8 and 16

| | |
|---|---|
| Role | The one screen font: 8x8 (the 240-line modes, at `$010000`) and 8x16 (640x480, at `$010800`); also the F12 menu's and side panel's own font, which must draw when the guest has wrecked everything. |
| Local path | `data/fonts/unscii/` -- `unscii-8.hex` and `unscii-16.hex` (source), `font8-unscii.bin` and `font16-unscii.bin` (kept as generated; the generator, `hex2chargen.py`, was removed 2026-09-14) |
| Upstream | https://viznut.fi/unscii/ (Viznut) |
| Version | Release **not determined** — the `.hex` is bare `codepoint:bitmap` rows with no header. Upstream was at 2.1. `data/fonts/unscii/VENDORED-FROM.txt` |
| Licence | public domain |
| Altered | No; converted to chargen form. |
| Verify | `sha256 5130fc27c18e32309d3a35f2b9e5f2d96650dc33e4f77a8b731ab74e5e41201e  font8-unscii.bin`<br>`sha256 a43c9d7f63b8a565ab4a77b80ad1269478d80499f82cf4155c22a7496c954d39  font16-unscii.bin` |

## Bomb Party

| | |
|---|---|
| Role | Art for the BOMBER game: walls, crates, bombs, blasts and the four little people become VICKY tiles and sprites. |
| Local path | `data/bombparty/` -- `bomb_party_v4.png` |
| Upstream | https://opengameart.org/content/bomb-party-the-complete-set (devurandom, with richtaur and cemkalyoncu; fetched 2026-08-28) |
| Version | v4, the "complete set" sheet; 15x19 cells of 16x16. |
| Licence | CC-BY 3.0 -- attribution required, given in CREDITS.md. |
| Altered | No. `tools/mkbomber.py` derives `demo/bomber.h` at build time; the derived file is not committed. |
| Verify | `sha256 1635752a826c6d4b0d3a793e635d6675244d1590dfc88560da85a682036b9f56  bomb_party_v4.png` |

## cc65

| | |
|---|---|
| Role | Build tool. The system ROM, the demos and the editors are C compiled with it; `.prg` binaries link its runtime (`none.lib`). |
| Local path | Not vendored. Built from source into `~/opt/cc65` on each build host. |
| Upstream | https://github.com/cc65/cc65 |
| Version | tag **V2.19**, commit `5552824`. Note that the binaries report `V2.18 - Git 5552824`: cc65 does not bump the version string until release, so `--version` understates it. `git describe --tags` is the reliable check. |
| Licence | zlib-style (applies to the linked runtime) |
| Altered | No. **Do not build from `master`** -- it deprecates the `sp` symbol the ROM's crt0 uses, and the resulting ROM hangs at boot with no diagnostic. |

## Circle and circle-libsdl2 (the retired bare-metal Pi port)

The port was removed on 2026-09-07; nothing in the tree links against
these any more. Kept for the record of tag `alpha-0.5`, the last release
that carried a `kernel8.img`.

| | |
|---|---|
| Role | The bare-metal runtime the Pi 3B+ port linked against. |
| Local path | Not vendored. Expected at `$(SHIM)` — default `~/Projects/k4510-pi/circle-libsdl2` (`pi/Makefile`). |
| Upstream | https://github.com/Xalior/circle-libsdl2 — **the shim, not `rsta2/circle` directly.** Circle, newlib, mbedtls and the rest arrive through it as submodules. |
| Version | `30cbcbd` (`vPoC3-155-g30cbcbd`), checked on the build host 2026-08-26. Circle itself is `6177984e` (tag `Step51`), circle-stdlib `a4fbed9` (`v8.0-620`). The full submodule list is in `pi/VENDORED-FROM.txt` at tag `alpha-0.5`. |
| Licence | GPL-2.0-or-later (Circle); the submodules carry their own. |
| Altered | No. |

## Wozmon

Not third-party code in the vendoring sense: `rom/wozmon.a` is a
reimplementation for the 45GS10 from Steve Wozniak's published 1976
listing, and carries this project's licence. Listed here so that a
reader looking for it does not conclude it was overlooked.

## Gaps in this record

Every component now has a `VENDORED-FROM.txt` beside it. Three of them
still cannot name an upstream commit, and say so rather than guessing:

| Component | What is missing | Why |
|---|---|---|
| `core/xemu/` | the commit | no version in the sources |
| `data/fonts/unscii/` | the release | the `.hex` has no header |

Each record carries the command that would settle it from a machine with
network access. A guessed hash in a provenance file is worse than an
honest gap, so none were guessed.

## Re-check commands

    # a directory digest, as used above.  The same command for every
    # component -- .asm/.s/.inc for Tali Forth and EhBASIC.
    # LC_ALL=C matters: without it the sort order and the digest change.
    dir_digest() {
      find "$1" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cc' \
           -o -name '*.asm' -o -name '*.s' -o -name '*.inc' \) -print0 \
        | LC_ALL=C sort -z | xargs -0 cat | sha256sum | cut -c1-16
    }
    dir_digest core/xemu

    # a single artefact
    sha256sum data/fonts/unscii/font8-unscii.bin

    # the vendoring records already in the tree
    cat cpm/VENDORED-FROM.txt forth/tali/VENDORED-FROM.txt
    cat tube/ALTERED.md data/fonts/README.md

    # the toolchain actually in use (the version string understates it)
    cc65 --version; git -C ~/opt/cc65 describe --tags

## Text fonts at the panel's pixels (data/fonts/hd)

| | |
|---|---|
| Role | F12 -> Video -> Font: at 720x540 the HD text (each 8x16 cell as 16x32, 8x8 as 16x16: `core/vicky.h`, `vicky_hd_font`); at 1440x1080 the machine's own 16-wide font. The machine's 8-wide font in RAM stays unscii. |
| Local path | `data/fonts/hd/` -- `<face>-{k4510,cp437}.bin` (16x32) and `<face>16-*.bin` (16x16), generated by `tools/mkhdfonts.py`; `VENDORED-FROM.txt` names each source |
| Upstream | Terminus Font (Dimitar Toshkov Zhekov) as Ubuntu 24.04's console-setup ships it; Spleen (fcambus); the Linux kernel's `font_8x16.c`; Atkinson Hyperlegible Mono and Fira Mono (google/fonts); Go Mono (golang/image); Proggy Clean (bluescan/proggyfonts); Tamzen (sunaku/tamzen-font) |
| Version | All fetched 2026-10-06; each source's sha256 is in `VENDORED-FROM.txt` |
| Licence | SIL OFL 1.1 (Terminus Font -- Reserved Font Name, so *Zhekov* here; Atkinson Hyperlegible Mono; Fira Mono); BSD-2-Clause (Spleen); BSD-3-Clause (Go); MIT (Proggy); GPL-2.0 (VGA); permissive (Tamzen) |
| Altered | Re-laid in the K4510 code page's and CP437's order, 256 glyphs; TrueType faces drawn at 16x32 without anti-aliasing; 8-wide faces doubled; 16x16 from the 16x32 by ORing row pairs; missing glyphs from unscii-16; box drawing from the VGA for drawn and doubled faces. |
| Verify | `sha256 51249657c1b76ea2140182b56ff5874f61637c064684d677d4d5c5e3e2af6752  atkinson16-cp437.bin`<br>`sha256 6834fdc688f99c5bfe95637d63408a397375b0374cc08b6bd292206160f9d120  atkinson16-k4510.bin`<br>`sha256 147245e7bcf8299cdcb14db4b514d24abebd0903bcf6c5623252f42c122d1878  atkinson-cp437.bin`<br>`sha256 78ff5b1f687fc19dacf99e950ae4d1d50ef751c4acc5a4f87260bba9ad218e95  atkinson-k4510.bin`<br>`sha256 ae554ae993606042831d47fad672db653abb3dcbf050bc4840413e26ac3d3a53  fira-mono16-cp437.bin`<br>`sha256 f42f0d828acba16095c33b11170249359f9920a6c9a59bc274831e7bade879ac  fira-mono16-k4510.bin`<br>`sha256 2afb962c28fe9917fbfbd673c679a2d51f51500609a0cacf82f9d5fb690c9e24  fira-mono-cp437.bin`<br>`sha256 f57024326ec0088e7c4d61caa755619a239a8412b7fb605ee1a9e9acaef9764b  fira-mono-k4510.bin`<br>`sha256 d75e5f7816a61c54a00917d27f6e1c971710b4888232c84d3c6f36206d005029  go-mono16-cp437.bin`<br>`sha256 3792c78837da73e27cb2da2aa4db093b7aa25dfc1f18df6af846f619f6ba4755  go-mono16-k4510.bin`<br>`sha256 d4721326bf74aef28964a158c29d356d25bd7ae1f26a8b24c9792f49bc3128f7  go-mono-cp437.bin`<br>`sha256 f89169c72140d65d13dbb8c78ef359295914e2b7e0f89e5a7bdd10b8b30e4a68  go-mono-k4510.bin`<br>`sha256 abb178994ff6bc92367693010da5f2694b5621f3aef201dafa3176c6cd3faab7  ibm-vga16-cp437.bin`<br>`sha256 c9a0291302fa3b270231a7ddc3fbbb06f7d49c24a62245ac93d94d68b568e07d  ibm-vga16-k4510.bin`<br>`sha256 09d0e0eab780aa31106255367c9f6b10bdc561f7ac04e1241165f96c5db011aa  ibm-vga-cp437.bin`<br>`sha256 43d40ba2e532f67e891eb93f2be913d4eef1124b67d1f63b7dfd9cd43b7b8db7  ibm-vga-k4510.bin`<br>`sha256 1246aee16af4ca80d5f16940393994665ef7f9d010b3909707b29e3e360e1d2e  proggy16-cp437.bin`<br>`sha256 6d00076246b06651ac70e02983cb03723e0a1d03cb0468c81a74c8ebf819af36  proggy16-k4510.bin`<br>`sha256 1ec92fcef0fd6185c931dd5459b09333c31ed83762833374c4006978059e4f8e  proggy-cp437.bin`<br>`sha256 c267d058977c281bb1236e9b2ba00d1f29351837211e9eb010f4ad2241bc8824  proggy-k4510.bin`<br>`sha256 0652448e8820968139ead8a2226a1a51cbaf10111f811c01305c57a5c74a8fe0  spleen16-cp437.bin`<br>`sha256 2d1697a525d236ed65f72390e2b3d1c0d5a21c2e12ecfeb1f31d09a2b2554a25  spleen16-k4510.bin`<br>`sha256 ef1fe12d5ac739688a79e77fe24aa6706745bb7f508c9243589de1fb555a2472  spleen-cp437.bin`<br>`sha256 4506c0805c8f1175f48a4adfe2de1abbbf4bfbf9349c1059f82b41beb5c750ac  spleen-k4510.bin`<br>`sha256 28435fcabbb7be08efd64aa25040c254ff72a89a192f27f44f097d05964eb61e  tamzen-bold16-cp437.bin`<br>`sha256 d225772f4815ca8c11d66b4391b0e30eb31487d136f0e952a2e7a6a210a8aa19  tamzen-bold16-k4510.bin`<br>`sha256 e0af53c0ebf5541a76de1285d4fdbfc2c5a7c2e6b18758e1d563cf49a7f315a3  tamzen-bold-cp437.bin`<br>`sha256 46443d34c5d9eec581802fd6fb676f67b788b92e6337b8d8ae663927eac1370e  tamzen-bold-k4510.bin`<br>`sha256 85801308725d1da1c2401bda534e0421426526276981f8063c6579571c64e15f  zhekov16-cp437.bin`<br>`sha256 1bd7a01e15e0f768c830bc435e7476e96436d5281db1d9e4441feebd2d046a8d  zhekov16-k4510.bin`<br>`sha256 3279b950b56e63648413423591baa8533608ae32329cd00fbd6f193bd08b2101  zhekov-bold16-cp437.bin`<br>`sha256 927f73f3aba4e5e4bdc0108e4abd55d68dd8094452befaf831967394f3621614  zhekov-bold16-k4510.bin`<br>`sha256 6da1ea8cc2b69ee2622e6ba63d7173dbfec203169d2df1352b6d3b4b918e3374  zhekov-bold-cp437.bin`<br>`sha256 11e634dc85bfa75a327734f1b2d1fe380ef8af2b8c8d93e4331f43222843faf4  zhekov-bold-k4510.bin`<br>`sha256 c19798bcb1d7f644c62189b3941043af3458c22004dbf8c1e891607b6bfce694  zhekov-cp437.bin`<br>`sha256 9e06ca91d8b00477c2963c116e8a9b52ae8dfef58137332b28cb2af064d14357  zhekov-k4510.bin` |
