# Camera switch

Lily Hewitt asked for this on 2026-11-12: a webcam kill switch, "like nothing can access it if its
toggled off". Mac OS 9 had no webcam switch, so **Camera** is an original Control Strip module. Its
menu is worded like 9.2.2's AppleTalk Switch:

- **Camera On** / **Camera Off**, the current one checked.
- A line, then the cameras it covers, in italics. A camera still listed as **(Still On)** while the
  switch is Off was not blocked; choose Camera Off again.
- Cameras it cannot switch are listed as **(Can’t Be Turned Off)** (see below).

The module's picture is a webcam. When the switch is Off, the lens is closed and a red line strikes it through.

**Camera Off** never asks for a password, because it only takes access away. **Camera On** asks for an
administrator's password every time, so no program can quietly turn the camera back on.

## What Off does

The kernel does the blocking. Programs aren't asked to stop using the camera.

- Every USB camera interface is **deauthorized**: one of the USB Video Class (`bInterfaceClass` 0e,
  every ordinary webcam, built in or plugged in), one with a video device, or one whose USB ID is a
  gspca driver's. gspca is Linux's driver family for older webcams, such as Lily Hewitt's PS2 EyeToy
  (054c:0154/0155, `gspca_ov519`). It is found with `modprobe -R` on the interface's USB ID, which loads
  nothing, so a blocked EyeToy is still known after its video device has gone or after a restart.
  The kernel unbinds its driver at once and won't bind one again. A program using the camera loses
  it right away and its `/dev/video*` disappears. The webcam's microphone is a separate audio
  interface and keeps working. Nothing else on the USB device changes.
- `/etc/udev/rules.d/70-platinum-webcam-off.rules` deauthorizes video interfaces as they appear, so
  the switch also covers a camera plugged in later and every restart. A second rule deauthorizes the
  USB interface under any video device that appears anyway (a gspca camera whose driver was still
  loaded). It only matches USB devices, never a virtual camera (v4l2loopback) or a PCI capture card.
- `/etc/modprobe.d/platinum-webcam-off.conf` stops `uvcvideo` and `gspca_main` from loading at all.
  Every gspca driver needs `gspca_main`, so none of them can load. Nothing can open a camera at boot
  before the udev rule runs. The helper also unloads them: gspca's own drivers, then `gspca_main`, then
  `uvcvideo`.

**Camera On** removes both files, loads `uvcvideo` (and each other camera's own driver, by its USB
ID, such as `gspca_ov519`), authorizes the interfaces again and probes them.

The helper only ever replaces or removes its own two files (each begins with `# platinum-webcam`).
If either path holds someone else's file, it changes nothing and says so.

## Limits

- **Not USB:** some newer laptops (for example Intel IPU6 MIPI cameras) don't use a USB camera.
  The switch can't block those. They are listed so the menu never claims a camera is off when it isn't.
  Virtual cameras (v4l2loopback, OBS) aren't cameras, so they aren't listed.
- **USB video capture that isn't a camera** (a TV tuner or capture dongle) has a video device too, so
  it is switched with the cameras. For a privacy switch that errs the right way.
- An administrator can always undo it (for example `sudo rm` the two files). Off protects you from
  programs, not from someone with your administrator password.

## Pieces

- `scripts/platinum_webcam.py`: the logic, which takes a root folder so tests can use a fake sysfs.
  `scripts/platinum-webcam` is the helper that pkexec runs as `/usr/lib/platinum-reality/platinum-webcam`.
  `status` needs no administrator.
- `packaging/org.platinumreality.webcam.policy`: two polkit actions for the one helper, picked by
  its first argument (`exec.argv1`). `…webcam.off` is allowed for the active session's user;
  `…webcam.on` needs `auth_admin`.
- `src/desktop/camera_switch.cpp`: the desktop's side. It runs the real helper through pkexec only
  in a real session, so tests can never switch the host's camera.
- Tests: `tests/platinum_webcam_tests.py` (ctest `camera-switch`), and
  `DesktopTests::cameraModuleSwitchesTheCamera` for the module and its menu.
