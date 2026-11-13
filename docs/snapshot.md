# Snapshot

Lily Hewitt, 2026-11-12: "lets make a super simple photo booth. basically to take basic photos with
webcam and test its working". Mac OS 9 had no such program, so **Snapshot** is an original window in
the Platinum style (the name is Lily's choice). Open it from the Apple menu ▸ Snapshot, or from
Applications.

## The window

- **The picture:** the camera's live picture, in a sunken well (480 × 360).
- **Take Picture** (Return, or File ▸ Take Picture, Command-T): the picture flashes white and is
  saved. The line beside it says where, and the last picture shows as a small thumbnail.
- **Show Pictures** (also in the File menu): opens the folder they're saved in.
- **Camera:** with more than one camera, a pop-up menu chooses which one. With one, it isn't shown.

Pictures are saved as JPEG (quality 92) in `~/Pictures/Snapshot` (the Pictures folder from the
user's XDG settings), named for when they were taken: `Snapshot 2026-11-12 at 21.05.09.jpg`, so
they sort in order. Two in the same second get "… 2.jpg", and an earlier picture is never replaced.

## Without a camera

When there is no camera, or the camera disappears, the picture says **No camera is available.** and
explains: connect a camera, or turn the Control Strip's **Camera** switch On
([camera-switch.md](camera-switch.md)). Take Picture is dimmed. Cameras are watched while the window is
open, so the picture comes back by itself when a camera does: plugged in, or switched On. If a
camera stops (in use by another program, or switched Off), Snapshot says that instead. That makes
Snapshot the quickest way to check the Camera switch: switch it Off and the picture goes; switch it
On and the picture returns.

## How it works

`src/snapshot/`: Qt Multimedia (`QMediaDevices`, `QCamera`, `QMediaCaptureSession`, `QImageCapture`,
`QVideoWidget`) with its FFmpeg backend. Older webcams (gspca, such as the PS2 EyeToy) send formats
Qt may not take, such as raw JPEG frames, so Snapshot starts itself once more with libv4l's
`v4l2convert.so` preloaded when `v4l-utils` is installed. That offers every camera's picture in plain
formats too (`PLATINUM_SNAPSHOT_NO_LIBV4L=1` turns it off). `qt6-multimedia-ffmpeg` (which brings
`qt6-multimedia`) and `v4l-utils` are optional dependencies, so an update never needs software that isn't
installed (Software Update refuses that). Without Qt's camera support, choosing Snapshot says what to
install instead of opening. The captured frame is saved by Snapshot itself (`SnapshotWindow::savePicture`), so
the name, format and "never replace" rule are its own, whatever the backend.

## Tests

`tests/snapshot_tests.cpp` (ctest `snapshot`):

- with no camera: the message, Take Picture dimmed, nothing written;
- saving: JPEG, the name, three in one second all kept, the thumbnail;
- with a camera (skipped where there is none): wait until it is ready, check the preview is shown
  the camera's frames, Take Picture, and check the saved picture is the camera's (not one flat colour).

Proven 2026-11-12 in the test VM with a v4l2loopback camera fed FFmpeg's moving test pattern
(640 × 480 YUYV), both directly and through libv4l: the preview received frames and the saved JPEG
was the pattern. Not yet seen: the live picture on a real screen with a real webcam (the VM had no
display), and Lily Hewitt's EyeToy itself.
