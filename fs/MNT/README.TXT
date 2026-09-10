/MNT -- other disks.

/MNT/SHARE   the one folder the container shares with the desktop it
             runs on (podman.sh): COPY /MNT/SHARE/FOO.BAS /HOME/ brings a
             file in, the other way sends one out.
/MNT/USB     a stick, where the appliance mounts one.

A server can be mounted here and then navigated like a local disk:

    MOUNT tnfs://atari8.us /MNT/ATARI
    CD /MNT/ATARI
    DIR
    CD "4] APPLE_II"        (quotes for a name with spaces)
    RANGER                  (works: the cwd is local, so a program runs)
    UMOUNT /MNT/ATARI

http:// and https:// mount too (read-only, like tnfs://).  A URL is still
a file name on its own (TYPE http://host/file), and CD tnfs://host/dir
still makes a remote directory the current one without a mount.
