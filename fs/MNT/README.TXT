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

http://, https://, ftp:// and sftp:// mount too, read-only (ftp/sftp go
through the Linux host's curl and ssh).  A URL is still a file name on
its own (TYPE ftp://host/file), and CD tnfs://host/dir still makes a
remote directory the current one without a mount.  Addresses may be
written the FujiNet way too: N:SFTP://user:pass@host/path.

For a shell on another machine:  SSH [user@]host  (the host's ssh).
