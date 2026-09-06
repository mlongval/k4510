# K4510x: on tty1, and only there, become the machine.
#
# exec, so there is no shell waiting behind it and no way to end up with two.
# On any other tty -- Ctrl+Alt+F2, or ssh -- this does nothing and you get an
# ordinary Debian login, which is the whole difference between K4510x and the
# bare-metal appliance: the Linux underneath is meant to be reachable.
#
# Quitting the emulator (F7 -> Quit) falls out of the exec and back to a login
# prompt on tty1, because agetty respawns.  That is deliberate: on the Pi that
# menu entry halts the board; here it should hand you the host.
if [ "$(tty)" = "/dev/tty1" ] && [ -z "$K4510X_NO_AUTOSTART" ]; then
    export SDL_VIDEODRIVER=kmsdrm
    export SDL_AUDIODRIVER=alsa
    # --host-shell: `!cmd` at the machine's prompt runs cmd in this Linux, and
    # a bare `!` is a shell.  K4510x is the one build where that is wanted.
    cd "$HOME/k4510" 2>/dev/null && exec ./sdl/k4510 --host-shell
fi
