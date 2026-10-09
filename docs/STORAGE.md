# What is on the disk, how the machine boots, and what ends up where

Doc, 2026-09-17: "please give me an overview of the disk structure (on disk)
and then the boot process and what ends up where." Figures are the Dell's, that
day, after `/live` was given its own partition.

## 1. On the disk

One NVMe drive, shared with Fedora. Two of its five partitions are the K4510's.

| | Size | What | Whose |
|---|---|---|---|
| `nvme0n1p1` | 600 MB | EFI system partition: Fedora's shim and GRUB | Fedora |
| `nvme0n1p2` | 2 GB | Fedora's `/boot`: its kernels, and **`grub2/grub.cfg`**, which holds the K4510's two menu entries | Fedora |
| `nvme0n1p3` | 206 GB | Fedora itself (btrfs). `/etc/grub.d/42_k4510` here is the template those entries are regenerated from | Fedora |
| `nvme0n1p4` | 25.8 GB | label **`K4510`** — everything the machine *saves* | K4510 |
| `nvme0n1p5` | 4 GB | label **`K4510LIVE`** — everything the machine *is* | K4510 |

**`K4510LIVE` (p5) — the system. Read once a boot, never written except by a deploy.**

    /live/vmlinuz               12 MB   the Linux kernel
    /live/initrd.img            75 MB   the initramfs, with live-boot in it
    /live/filesystem.squashfs  834 MB   the BASE: Debian, SDL, fonts, compilers. Changes a few times a year
    /live/k4510.squashfs         7 MB   the LAYER: the emulator, the ROM, fs/, the tools, our units.
                                        This is what `tools/k4510-deploy` replaces

**`K4510` (p4) — what is saved. Stays on the disk; read and written on demand.**

    /persistence.conf                   which directories are kept (three, below)
    /home/k4510/rw/                     the "upper" of /home/k4510: every file changed or
                                        created under the user's home, and nothing else
        k4510/k4510.cfg                 the F12 settings
        k4510/fs/HOME/, /DOCUMENTS/...  whatever was saved on the machine
        k4510/shots/                    PrtSc screenshots
    /etc/NetworkManager/.../rw/         the Wi-Fi connections
    /var/lib/tailscale/rw/              the tailnet identity
    /backup/k4510.squashfs.<sha>        the last three layers, for rolling back   (20 MB)

p4 holds **59 MB** today. It is only ever a *difference*: a file that shipped
in the layer and was never edited is not here at all.

## 2. The boot

1. **Firmware -> Fedora's GRUB.** `saved_entry` remembers the last system
   chosen, so the laptop comes back to whichever was used last.
2. **GRUB -> the K4510 entry.** It finds the partition labelled `K4510LIVE`,
   loads `/live/vmlinuz` and `/live/initrd.img` from it, and passes:

        boot=live live-media=/dev/nvme0n1p5 toram
        persistence persistence-label=K4510 union=overlay

3. **initramfs: `toram`.** live-boot makes a tmpfs at `/run/live/medium` and
   copies **the whole medium** -- all of p5, 887 MB -- into it. p5 is then
   finished with. (Until 2026-09-17 the medium was p4, and "the whole medium"
   was 2.2 GB: screenshots, old layers and WADs included.)
4. **initramfs: the root.** Both squashfs files are mounted *from that RAM
   copy*, read-only, and stacked: layer over base. A second tmpfs goes on top
   as the writable upper. That overlay is `/`.
5. **initramfs: persistence.** p4 is mounted at
   `/run/live/persistence/nvme0n1p4`, and for each of the three directories in
   `persistence.conf` an overlay is laid over the RAM one, with its upper **on
   p4**. So `/home/k4510` = base + layer (RAM, read-only) + `rw/` (disk).
6. **systemd, about 3.5 s.** NetworkManager, tailscaled, `k4510-brightness`.
   `k4510` is logged in on tty1 automatically and its profile starts
   `~/k4510/sdl/k4510` straight on the KMS display: no X, no desktop.
7. **The emulator** loads `rom/kernal.bin`, points the storage device at
   `~/k4510/fs`, and the 45GS10 comes out of reset into K/OS: the banner,
   `/STARTUP.BAT`, the prompt. About 26 s from the power button, 8-13 of them
   the firmware's.

## 3. What ends up where

| Thing | At run time it is | Lives in | Survives a reboot? |
|---|---|---|---|
| Linux, libraries, SDL, compilers | RAM | base squashfs, in tmpfs | it is re-copied |
| The emulator, the ROM, `/SYSTEM`, `/APPS`, `/LANG`, the handbook | RAM | layer squashfs, in tmpfs | it is re-copied |
| A shipped file nobody has edited (`/APPS/CHESS/README.TXT`) | RAM | the layer | yes, and a deploy updates it |
| A shipped file that WAS edited on the machine (`SHIPPING.CFG`) | **disk** | p4 `rw/` -- it **shadows** the layer's copy | yes -- and a deploy can no longer change it |
| Anything created on the machine: `/HOME`, `/DOCUMENTS`, programs you write | **disk** | p4 `rw/` | yes |
| `/DISK/...` -- games, downloads, anything large | **disk** | p4 `rw/` | yes |
| `k4510.cfg`, Wi-Fi, the tailnet identity | **disk** | p4 | yes |
| Old layers | **disk** | p4 `backup/` | yes |
| `/tmp`, logs (`/tmp/k4510-emulator.log`), anything outside the three directories | RAM | the root's tmpfs upper | **no** |

RAM after boot: ~0.9 GB for the system images plus ~0.8 GB in use, of 32 GB.

## 3a. The extras: on the disk, loaded as needed

Doc, 2026-10-08: "can the extra megabytes be only loaded as needed?"  Yes:
programs too big to pay for in RAM at every boot -- mpv, ffmpeg and their
libraries, ~100 MB -- are one squashfs file on **p4**, not in `/live`:

    /extras/extras.squashfs             mpv, ffmpeg + what they need that the base lacks

| | |
|---|---|
| made by | `sudo k4510-extras build [pkg...]` on the machine itself (apt works out what the running system lacks; `dpkg -x`; mksquashfs) |
| found at boot | `k4510-extras-link.service`: a link `/run/k4510-extras.squashfs`, nothing read |
| mounted | by systemd's automount, the first time anything looks in `/opt/extras`; let go after 10 idle minutes |
| run | `/usr/local/bin/mpv`, `ffmpeg`, `ffprobe` are `k4510-extra`: the program in `/opt/extras` with its libraries on `LD_LIBRARY_PATH` |
| costs | disk; RAM only for the pages actually read, as page cache |
| a deploy | does not touch it; `k4510-extras status`, `sudo k4510-extras remove` |

A stick without persistence has nowhere to keep it.

## 4. The rules that follow

- **Everything in the layer costs RAM at every boot; nothing on p4 does.**
  Shipped = small. Anything large the machine can start without goes in
  `/DISK`, fetched or copied there on the machine (`fs/DISK/README.TXT`).
- **Disk files are not slow.** Linux keeps what it has read; a WAD is read
  from the NVMe once and from the page cache after that.
- **An edit on the machine wins over a deploy**, for that file, for ever --
  the upper shadows the lower. To take the shipped version back, delete the
  file from `rw/` (on the machine: just delete it, and the layer's shows again).
- **A deploy touches p5 only** (`tools/k4510-deploy`): the new layer into
  `/live`, the old one into `backup/` on p4. Rollback: copy one back.
  Never keep a second `*.squashfs` in `/live` -- live-boot stacks every one it
  finds there.
- **A USB-stick K4510** has one partition and usually no persistence: all of
  the above with "disk" read as "gone at power-off".
- **A new internal install is made this way from the start**:
  `install-k4510.sh` carves `K4510LIVE` off the end of the `K4510` partition
  (and converts a one-partition install the next time it is run, from the
  host OS, where that partition is not mounted). `k4510-split-live` is the
  same operation for a machine that has no other OS to do it from.

## 3b. The personalities: other machines, on the disk, into RAM when chosen

Doc, 2026-10-08: a boot list of "personalities, like FPGA images" -- the
K4510 first, then the C64, C128, PET, Amiga and (added the same night) the
Commander X16.  Hold SPACE at power-on:
GRUB boots the K4510 as always, and on tty1, just before the emulator,
`k4510-boot-menu` waits one second for the (autorepeating) space and lists
them -- the K4510 first and chosen.  The one chosen runs from RAM; quitting
it brings up the K4510.  No space: the K4510, one second later.  (GRUB has
no entries for them: Doc wanted the choice made by Debian, not the loader.
`k4510.personality=NAME` on the kernel command line still starts one.)

    /personalities/vice.squashfs    ~5 MB   x64sc, x128, xpet (VICE 3.10, SDL2 UI) + ROMs
    /personalities/amiga.squashfs  ~18 MB   Amiberry 8.3 (SDL3) + Kickstart 1.3 and 3.1
    /personalities/x16.squashfs   ~0.2 MB  x16emu r49 + the X16 ROM (CBM KERNAL/BASIC licensed for the X16)
    /personalities/*.list                   NAME<TAB>title of each machine in the image

| | |
|---|---|
| made by | `~/Projects/K4510-Personalities` on ubuntu-s1 (`tools/make-images.sh`), deployed there too |
| in the menu | `k4510-boot-menu` (profile.d, tty1) reads the `.list` files; a family shows only while its image is on p4, and with none there it does not wait |
| run | `k4510-personality NAME`: the family's image is copied to `/run` (RAM) and mounted at `/opt/personalities/<family>` -- only when one is chosen |
| saves | `~/personalities/NAME/` -- disks, the A1200's hard drive (a directory, Workbench 3.1), `.uae`; VICE's settings in `~/.config/vice`; the X16's drive 8 is `~/personalities/x16` itself (HostFS: plain files), plus `nvram.bin` |
| quitting | VICE and the Amiga: F12 -> Quit; the X16: Alt+F4, or POWEROFF at its BASIC prompt |
| a deploy | of the layer does not touch them |
