/MNT -- other disks.

/MNT/SHARE   the one folder the container shares with the desktop it
             runs on (podman.sh): COPY /MNT/SHARE/FOO.BAS /HOME/ brings a
             file in, the other way sends one out.
/MNT/USB     a stick, where the appliance mounts one.

The network is not here: a URL is a file name (TYPE http://host/file)
and CD tnfs://host/dir makes a remote directory the current one.
