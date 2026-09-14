# The Linux Underneath

Every emulated machine sits on a real one. On most of them that fact is carefully hidden; here it is a feature, because the real machine has compilers on it and the fantasy one is being used to write programs.

This chapter is about the seam — the one character that crosses it and the words that use it — and then about the Linux itself: what is on it, how it gets onto a stick or beside another system, and how it boots.

## `!` — the Linux prompt, from the machine’s prompt

A line beginning with `!` is not the machine’s:

    !ls -l
    !git status
    !nvim notes.txt

runs that command on the Linux the machine is standing on, with its output drawn on the machine’s screen. A bare `!` with nothing after it gives you an interactive shell there; leave it the way you leave any shell.

It is carried on the Tube ([Chapter 6, The Tube: BBC BASIC](06-tube.md)), which is the part of this machine already built to have another processor’s console on the glass: the host’s shell is started on the same pty the co-processors use, JIM renders it, and the ROM’s own key loop feeds it. So the colours stay the machine’s, full-screen programs work — `nvim`, `htop` and `tmux` run in the window — and `ls --color` lands in the machine’s palette. The shell is told its terminal is **xterm-color**, and it speaks UTF-8: JIM draws what it can in the machine’s character set and a near likeness of the rest, and an accented letter you type arrives as one.

`SSH [user@]host` is the same thing for a computer across the network: an ssh session, drawn by JIM, through the Linux’s own ssh.

!!! note ""
    **Locking the door.** On a machine a child uses, the way into Linux may be a way into trouble. `linux = locked` in the menu file ([The menu file: what the menu shows](01-machine.md#the-menu-file-what-the-menu-shows)) turns `!`, `SSH` and the menu’s telnet row away with a message, and `consoles = locked` makes the Linux consoles unreachable from the keyboard. `PAS` and `CC` still compile.

!!! note ""
    **What this means for the container.** The sandboxed flavour (`linux/podman.sh`) is a wall on purpose: it is handed the display, the sound, the game controllers and one folder, which the machine sees as `/MNT/SHARE`, and nothing else. `!` inside it is real, and reaches the container — which is a small Debian with the compilers on it, and not your computer.

## `PAS` and `CC`: compiling on the machine

The compilers live on the Linux side, so the machine reaches them the same way you would — but without you having to remember four options:

    VI HELLO.PAS
    PAS HELLO
    HELLO

`PAS name` compiles `name.PAS` *in the directory you are standing in* with Mad Pascal, and leaves `name.prg` beside it. `CC name` does the same for `name.C` with cc65, using exactly the rule the machine’s own Makefile uses for its programs. Intermediates go to a scratch directory and never appear on your disk; the compiler’s own errors appear on the screen, and its exit status comes back as the result code, so a script can test it.

That is the loop the machine owns: *edit the source on the machine, compile it from the machine’s prompt, run it on the machine.* The compiler itself still runs on the Linux beside it, which is why “self-hosted” has an “almost” in front of it — but the part that matters when you are writing a program is all on this side of the seam.

## What is on the other side

On a desktop, the other side is your own computer and has whatever you have put on it. On the K4510’s own Linux it is a Debian built for the purpose, and the list is short because everything on it is there for a reason:

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;">cc65, 64tass, nasm</td>
<td style="text-align: left;">the machine’s own toolchain — it can rebuild itself</td>
</tr>
<tr class="even">
<td style="text-align: left;">FPC + Mad Pascal</td>
<td style="text-align: left;">what <code>PAS</code> runs</td>
</tr>
<tr class="odd">
<td style="text-align: left;">git, neovim</td>
<td style="text-align: left;">so that work done here is work</td>
</tr>
<tr class="even">
<td style="text-align: left;">curl, ssh</td>
<td style="text-align: left;">what <code>ftp://</code>, <code>sftp://</code> and <code>SSH</code> go through</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>tek40xx</code></td>
<td style="text-align: left;">a Tektronix 4010 terminal, below</td>
</tr>
<tr class="even">
<td style="text-align: left;">telnetd</td>
<td style="text-align: left;">bound to the machine itself and nothing else</td>
</tr>
<tr class="odd">
<td style="text-align: left;">NetworkManager, Tailscale</td>
<td style="text-align: left;">the network: F7 → Host</td>
</tr>
</tbody>
</table>

</div>

**The consoles.** Ctrl+Alt+F1 is the machine; Ctrl+Alt+F2 to F6 are Linux text consoles, 80 by 25 in the IBM PC’s own font, where the Tektronix lives; Ctrl+Alt+F1 comes back. (On a laptop whose F-keys are media keys, add Fn.) Quitting the emulator (Shift+Esc, or F7 → Quit) drops you to a Linux shell on the machine’s own console rather than to nothing, and F7 has a *Shut down the computer* row because there is no desktop to go back to. The power button performs a clean shutdown. Closing the lid does what F7 → Host → *Lid closed* says: *keep running*, to begin with, or *suspend*. (Quit to the Linux shell and nothing is keeping the machine awake: there the lid suspends.) The consoles speak UTF-8 and follow the keyboard layout chosen in F7.

**The network.** F7 → Host shows the computer’s name and address, and *Wi-Fi / network setup* opens NetworkManager’s own screen on a spare console to join a network; the machine is back when you leave it. A network joined once is remembered.

**Telnet, both directions.** `TELNET 127.0.0.1 23` from the machine’s prompt — or F7 → Host → *Telnet into the host* — logs in to the Linux underneath through the front door rather than through `!`, which is useful when you want a session that survives what the machine is doing. The daemon listens on the machine itself only, so there is no network path to it at all. Outbound, the network is real: `TELNET` reaches a BBS, and URLs work as [Chapter 2, The Shell](02-shell.md) describes.

## On a stick

    sudo ./linux/build-live.sh

makes the image, and `dd` puts it on a USB stick. At boot a menu offers the keyboard layout, and then the whole system is copied into RAM, so the stick is not being read while you work. A persistence partition on the same stick keeps what should survive a restart: the machine’s home directory — `k4510.cfg`, the menu file, your saved states and the machine’s disk with your files on it — the networks you have joined, and Tailscale’s identity. Leave the stick in.

**The laptop’s own drive is not there.** The stick’s kernel command line blacklists the storage drivers absolutely, so the laptop’s disk is not mounted, not visible, and not writable by anything on the stick. That is not a policy setting you could turn off by accident; the machine cannot see it.

## Beside another system

A computer that already runs Linux can have the machine as a second choice at power-on. Make an empty partition of 2 GB or more on its internal disk and label it `K4510`; boot the computer’s usual Linux with the stick plugged in; and from the stick run

    sudo ./install-k4510.sh

It formats that partition the first time (and never again), copies the system onto it, arranges for your settings and files to be kept in the rest of the partition, and adds two entries to the computer’s boot menu:

K4510 Fantasy Computer  
the machine, quietly: the machine’s logo while Linux starts and nothing else, then the prompt.

K4510 (text boot)  
the same system, showing Linux’s messages as it starts — the one to choose when something needs looking at.

It refuses a partition on a USB disk, and one on the stick itself, and it touches nothing of the other system but its boot menu. Run it again from a newer stick to update: the system is replaced and your settings and files are kept. The stick is only needed for the install.

**The boot menu.** The computer boots whichever of the two systems was used last, so the other system’s own updates, which restart it, come back to it. With the menu set to stay hidden, as it is on the computer this book was checked on, the computer starts at once; Esc as it starts shows the menu. That is the other system’s GRUB setting (`GRUB_TIMEOUT_STYLE=hidden`, `GRUB_SAVEDEFAULT=true` in `/etc/default/grub`), and the installer leaves it as it finds it.

The whole system is read into RAM at boot here too — everything on the `K4510` partition, which is why nothing large should be kept on it beside the system itself.

## A Tektronix beside the machine

`tek40xx` (Ian Schofield’s, GPL-3) is a Tektronix 4010/4014 storage-tube terminal that is also a telnet client. It is on the K4510’s Linux and in the container because of what a storage tube is *for*: a PiDP-11 answers on telnet ports, and a machine of that vintage deserves a screen of that vintage to draw on.

    tek HOST [PORT]

on the second console — full screen on a bare console, in a window under a desktop. And without any PDP-11 at all:

    tekplay NAME|FILE...

plays Tektronix plot files at a 9600-baud pace, which is the right speed to watch one being drawn. With no argument it plays every plot that ships; `HOME` clears the screen and `END` quits. Nothing in the emulator changed for any of this: it is a second program on the same computer, which is exactly what a second terminal was in 1975.

## From another computer

The machine’s Linux answers ssh (as the user `k4510`), which is how the menu file is edited once the machine is locked, and how a screenshot is taken from across the room:

    ssh k4510@machine k4510-shot

writes one, as PrtSc would, into `shots/`.
