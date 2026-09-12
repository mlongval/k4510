# K4510: on tty1, and only there, become the machine.
#
# exec, so there is no shell waiting behind it and no way to end up with two.
# On any other tty -- Ctrl+Alt+F2, or ssh -- this does nothing and you get an
# ordinary Debian login, which is the whole difference between K4510 and the
# bare-metal appliance: the Linux underneath is meant to be reachable.
#
# Quitting the emulator (F7 -> Quit) falls out of the exec and back to a login
# prompt on tty1, because agetty respawns.  That is deliberate: on the Pi that
# menu entry halts the board; here it should hand you the host.
# The machine's tools -- k4510-pas and k4510-cc, which PAS and CC at the
# prompt run through `!` -- on every tty, so they are the same command in a
# Linux shell as at the machine's prompt.
case ":$PATH:" in *":$HOME/k4510/tools:"*) ;; *) export PATH="$HOME/k4510/tools:$PATH" ;; esac
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
    # The same switch keeps the emulator's own account too: its stderr (tube
    # sessions, every way out, a heartbeat every ten seconds) in
    # emulator-<time>.log beside the byte log, and a core dump if it crashes --
    # core_pattern is "core", so ~/k4510/core, which persists.
    if [ -f "$HOME/k4510/DIAG/TERMLOG" ]; then
        TS=$(date +%Y%m%d-%H%M%S)
        export K4510_TERMLOG="$HOME/k4510/DIAG/termlog-$TS.bin"
        ulimit -c unlimited 2>/dev/null
        cd "$HOME/k4510" 2>/dev/null && exec ./sdl/k4510 2>>"$HOME/k4510/DIAG/emulator-$TS.log"
    fi
    cd "$HOME/k4510" 2>/dev/null && exec ./sdl/k4510
fi
