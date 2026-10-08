# The K4510x has no webcam driver

The K4510 Linux image (`linux/build-live.sh`) ships **without any webcam
driver**, on purpose.  Doc, 2026-10-08:

> Webcamera module is not SHIPPED by DEFAULT. It will require the end
> user/institution to enable them. I do not want some freak pirating this
> project and spying on kids.

A machine meant for children, which anyone may copy, should not carry the
means to watch them.  Turning the camera *off* is not enough: a driver that is
in the image can be loaded by anyone with root.  So it is not in the image.

## How

Four layers, from the build outwards:

1. **dpkg never installs them.**  `etc/dpkg/dpkg.cfg.d/k4510-no-camera`
   (in `linux/config/includes.chroot`) is in place before the kernel package
   is installed, with `path-exclude` lines for the USB camera drivers
   (`drivers/media/usb/{uvc,gspca,pwc,stkwebcam,zr364xx,s2255}`), so they are
   never unpacked -- and a kernel upgrade inside the image cannot bring them
   back.
2. **The build deletes any that got in and runs `depmod`** (`camera_check` in
   `build-live.sh`), on the full build and on the fast `REBUILD=1` path.
3. **The build fails** if `uvcvideo.ko` or `gspca_main.ko` is found in the root
   filesystem or in either squashed image (`camera_image_check`).
4. **The old soft switches stay** as a second line: `uvcvideo` is blacklisted
   (`etc/modprobe.d/k4510-no-camera.conf`) and any USB device with a video
   interface is de-authorized as it appears
   (`etc/udev/rules.d/91-k4510-no-camera.rules`), so even a driver brought in
   by hand does not bind.

A machine already installed from an older image (the Dell) gets the same
result from its next layer: `dell-build-here.sh` puts overlayfs whiteouts over
the base's camera module directories, so they are gone from the running system.

## The opt-in, for an institution that needs a camera

It is a build, deliberately, not a switch in the image:

    sudo K4510_CAMERA=1 ./linux/build-live.sh

That build leaves the dpkg exclusion, the blacklist and the udev rule out, keeps
the drivers, and says so as it starts.  It is your image, built by you, for a
purpose you are responsible for.  Nothing on a shipped K4510x turns a camera on.

`tools/k4510-vidcap` and VICKY's `$D548` have nothing to do with cameras: they
measure how many *pixels* the host can draw.
