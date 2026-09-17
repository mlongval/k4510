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
        k4510/fs/DISK/DOOM/*.WAD        downloaded games           (33 MB)
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
| A shipped file nobody has edited (`/APPS/DOOM/WADS.CFG`) | RAM | the layer | yes, and a deploy updates it |
| A shipped file that WAS edited on the machine (`SHIPPING.CFG`) | **disk** | p4 `rw/` -- it **shadows** the layer's copy | yes -- and a deploy can no longer change it |
| Anything created on the machine: `/HOME`, `/DOCUMENTS`, programs you write | **disk** | p4 `rw/` | yes |
| `/DISK/...` -- games, downloads, anything large | **disk** | p4 `rw/` | yes |
| `k4510.cfg`, Wi-Fi, the tailnet identity | **disk** | p4 | yes |
| Old layers | **disk** | p4 `backup/` | yes |
| `/tmp`, logs (`/tmp/k4510-emulator.log`), anything outside the three directories | RAM | the root's tmpfs upper | **no** |
| DOOM's frame buffer and sound rings | RAM | `/dev/shm` | no |

RAM after boot: ~0.9 GB for the system images plus ~0.8 GB in use, of 32 GB.

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
  the above with "disk" read as "gone at power-off". `install-k4510.sh` still
  makes ONE partition on a new internal install (docs/TODO.md); the split is
  done afterwards by `k4510-split-live`.
