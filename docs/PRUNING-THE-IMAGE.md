# Pruning the image that is loaded into RAM — parked

Doc, 2026-09-17: "I want to try and prune the image that is loaded into ram.
Suggestions?" -- and, having seen them: "I think I'm going to save it for
later. Seems more involved than I anticipated." So this is the measurement and
the plan, kept so that neither has to be done twice. Nothing here is built.

## What is loaded

887 MB a boot: `filesystem.squashfs` 834 MB (the BASE), `initrd.img` 75,
`vmlinuz` 12, `k4510.squashfs` 7 (the LAYER). **The layer is not worth
pruning.** `doomk4510` is 585 KB and all of `/APPS/DOOM` 30 KB; moving DOOM to
`/DISK` would save 0.07% and raise the question of what puts it there, since
nothing in `/DISK` ships.

The base is ~2 GB uncompressed. Measured on the Dell (`du` over
`/run/live/rootfs/filesystem.squashfs`, `dpkg-query` for packages), MB:

| What | MB | Needed at run time? |
|---|---|---|
| FreePascal (`fp-units-fcl`, `fp-units-rtl`) | 113 | No: only builds `mp` and `mads`, at image-build time |
| the Mad-Pascal clone under `~/Projects` | 103 | only `mp` and `lib/`, a few MB |
| `/usr/share/locale`, `i18n` | 115 | no |
| `/boot` inside the squashfs | 87 | no: the kernel and initrd are in `/live` already |
| stockfish | 75 | optional and large: a `/DISK` thing, like a WAD |
| `/usr/share/doc`, man | 58 | no |
| gcc, g++, cpp, their dev libraries | ~190 | only to rebuild the emulator ON the machine |
| git, perl, vim + nvim runtimes | ~160 | git no; perl and the editors partly (NVIM is a command) |
| firmware (iwlwifi 111, amd-graphics 89, intel 28, misc) | 241 | per machine: AMD's is dead weight on the Intel Dell |
| LLVM 123, Mesa gallium 40, z3 26 | ~190 | yes: how SDL draws on bare KMS. Leave alone |
| kernel modules | 99 | mostly |

## The order it would be done in

1. **The cuts that cannot break anything** (~475 MB uncompressed; image to
   perhaps 600-650 MB): exclude `/boot`; drop locales, doc, man; purge fpc
   once `mp`/`mads` are built; keep only `mp` + `lib/` of Mad Pascal.
2. **Stockfish to `/DISK`**, fetched on demand, CHESS saying so when it is
   missing. The first customer for a general `GET name` (WADCHOOSER is that
   command for one game).
3. **Build tools out of the running image** (~240 MB): the emulator is
   compiled inside the tree that gets squashed, which is the only reason gcc
   is on the machine. Build in one tree and ship another, or exclude at squash
   time. Costs `make` on the Dell itself; cc65 stays (`CC` is a command).
   Open question for Doc: does he ever rebuild the emulator on the Dell?
4. **Firmware by machine**: everything on the stick, a trimmed set for an
   internal install. Fiddly; a wrong cut is no Wi-Fi.
5. **Not touched:** LLVM/Mesa, kernel modules.

1-3 would land somewhere near 450-500 MB.

## Why it is more involved than a layer change

- It is a full BASE rebuild on p15 (`build-live.sh` without `REBUILD=1`), slow,
  and the base is what everything else stands on.
- `tools/k4510-deploy` moves only the layer. It would need to move the base,
  and keep the old one for rollback OUTSIDE `/live` or not named `*.squashfs`
  -- live-boot stacks every squashfs it finds there.
- Each cut wants the machine checked before and after: boot, Wi-Fi, PAS, CC,
  NVIM, DOOM, the Tube's BASIC and CP/M.
- A structural alternative, untested on a live-boot root: `systemd-sysext`,
  with the compilers in an extension image on the K4510 partition, overlaid on
  `/usr` from disk only when wanted. After the plain cuts, not instead of them.
