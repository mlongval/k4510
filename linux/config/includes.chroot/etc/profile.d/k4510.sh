# K4510: on tty1, and only there, become the machine.
#
# exec, so there is no shell waiting behind it and no way to end up with two.
# On any other tty -- Ctrl+Alt+F2, or ssh -- this does nothing and you get an
# ordinary Debian login, which is the whole difference between K4510 and the
# bare-metal appliance: the Linux underneath is meant to be reachable.
#
# Quitting the emulator (F12 -> Quit) brings up the Personality Chooser
# (k4510-session, below); until 2026-10-09 it fell back to a login prompt.
# The machine's tools -- k4510-pas and k4510-cc, which PAS and CC at the
# prompt run through `!` -- on every tty, so they are the same command in a
# Linux shell as at the machine's prompt.
case ":$PATH:" in *":$HOME/k4510/tools:"*) ;; *) export PATH="$HOME/k4510/tools:$PATH" ;; esac
# A UTF-8 locale on every login, or bash drops accented keys.  The only one
# this image has is C.UTF-8; ssh brings the client's LANG (en_US.UTF-8, say),
# which does not exist here and falls back to plain C.  Keep a LANG that
# works, replace one that does not.
if [ -n "$(LC_ALL= LANG="${LANG:-x}" locale 2>&1 >/dev/null)" ]; then
    export LANG=C.UTF-8; unset LC_ALL LC_CTYPE LC_MESSAGES LC_COLLATE
fi
if [ "$(tty)" = "/dev/tty1" ] && [ -z "$K4510_NO_AUTOSTART" ]; then
    export SDL_VIDEODRIVER=kmsdrm
    export SDL_AUDIODRIVER=alsa
    # A switch for chasing a terminal fault: while ~/k4510/DIAG/TERMLOG exists,
    # JIM logs every byte it is sent (K4510_TERMLOG) beside it -- persistent,
    # unlike /etc, so it survives the reboot that brings a new build.  Remove the
    # file to stop.  Doc, 2026-09-12 (stray characters under tmux + Claude Code).
    # An installed machine (the Dell) keeps its persistence on a fixed disk,
    # which live-boot mounts `sync` as if it were a stick about to be pulled:
    # 242 bytes/s for small writes, and the byte log stalled the machine to a
    # standstill (2026-09-12).  On a disk that cannot be pulled, async it; a
    # stick (removable) keeps its sync.
    for m in /run/live/persistence/*; do
        dev=$(findmnt -rno SOURCE "$m" 2>/dev/null); [ -n "$dev" ] || continue
        blk=$(lsblk -ndo PKNAME "$dev" 2>/dev/null); [ -n "$blk" ] || blk=$(basename "$dev")
        if [ "$(cat "/sys/block/$blk/removable" 2>/dev/null)" = 0 ] && findmnt -rno OPTIONS "$m" | tr , '\n' | grep -qx sync; then
            sudo -n mount -o remount,async "$m" 2>/dev/null
        fi
    done
    # From here tty1 is k4510-session's (Doc, 2026-10-09): the machine used
    # last -- the K4510 or a personality (C64, C128, PET, Amiga 500/1200, X16)
    # -- then, whenever one is quit, the Personality Chooser, round and round.
    # SPACE held or tapped during the splash shows the Chooser first.  Its
    # Power off and Restart, and the K4510's own F12 -> Power off, are the
    # only ways out; Ctrl+Alt+F2 and ssh still give a login.
    # k4510.personality=NAME on the kernel command line, the DIAG/TERMLOG
    # switch and the emulator's log in /tmp are all in k4510-session now.
    exec k4510-session
fi
