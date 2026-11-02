# Software-display buffer integration

The fallback passes automatic selection and actual client input on both GL and
plain virtio displays. All37 component suites, the new selection-policy suite
normally and with ASan/UBSan, and13 focused native EGL/OpenGL/Vulkan checks pass.
The complete release gate is still required before this candidate is offered.

## Automatic-selection verification

Runtime source35c9dc9, Qt6.12.0, Mesa26.2.5, Xwayland24.1.14, Sway1.12 and
SDDM0.21.0 in an owned2-vCPU/6-GiB Arch guest. Fresh extraction/runtime build,
pinned bridge, and explicit local package reinstall. Package SHA-256:
`f1db297827ea4b3076e73e1100cb8752f54acb26c74eea4f5935f905cd5fd4f8`.
After normal GL-guest poweroff, a complete disk copy referenced only the unchanged
original base, then booted with plain virtio-vga/display-none.

| Automatic startup | GL | Plain virtio |
|---|---|---|
| Integration selected | linux-dmabuf-v1 | wayland-egl; no usable DRM node on actual EGL device |
| DMA-BUF global | Present | Absent |
| Shared memory | Present | Present |
| Xwayland original launcher | Ready | Ready; previous startup timeout absent |
| Real X11 client pixels/input | Pass | Pass |
| Real Wayland client pixels/input | Pass | Pass |

No hardware-integration override or glamor-off argument in either session.
Each of four probes received one real QMP pointer press and eight key presses/
releases, and displayed platinum in the inspected composed image. Native fixture
inspection reads the text and event counts; no text-injection API is used.
Password login went through SDDM. Guest normally powered off23:43:49Z; shared
resource lock released. Full-release checks do not inherit these results by
assumption: they run separately against their frozen source/build hashes.

The first full gate subsequently caught a Ymir fixture assumption: its idle-color
OK-button locator inherited a pointer hovering over that button. Captured pixels
showed the correct dialog with a highlighted button. The fixture now moves the
pointer to a neutral menubar area before inspection, preserving every click,
fullscreen, Settings and profile assertion. Three consecutive complete Ymir
workflow repetitions pass; a fresh full gate is required for the corrected source.
Production inputs remain identical to the VM build; only the native test fixture
and this documentation changed after automatic VM verification.

## Reproduced boundary

On source493846a with Qt6.12.0, Mesa26.2.5 and Xwayland24.1.14, the plain
virtio-vga guest displays the desktop and accepts native Wayland input, but
Xwayland times out. Its compositor advertises DMA-BUF despite reporting that
the EGL device has no DRM device. The unchanged virtio-vga-gl baseline works.

Two guest-only experiments independently restore actual X11 pixels and input:
selecting the known wayland-egl compositor plugin (no DMA-BUF global; wl_shm
remains), and restoring that choice before appending -glamor off to the private
Xwayland launcher. Each also passes native Wayland pointer/keyboard input.
Successful clients receive an actual virtual mouse press and eight key presses/
releases for platinum; the composed images show the text. No SetText injection.
The experimental files were restored and the guest was powered off.

This supports the buffer-integration boundary, rather than identifying the exact
stalled protocol request. No protocol-level root-cause trace is claimed.

## Candidate policy

`graphics_integration.cpp` queries the EGL display supplied by Qt's platform
native interface, then its import capability, associated device, DRM node and
drawable format count. Finding an unrelated /dev/dri node is insufficient.
Only an eligible Wayland/OpenGL session with an installed DMA-BUF plugin is
probed. Explicit hardware/client integration choices and other renderer paths
are preserved. The existing drawable-format filter still applies on Qt6.12.

If those capabilities are absent, request the known wayland-egl plugin. Qt's
shared-memory support is independent of that plugin's optional accelerated
import path. The Arch qt6-wayland package supplies both integrations. An unknown
plugin name such as none does not disable hardware integration: Qt falls back
to an available plugin. The test-only no-GPU-buffer control now uses the known
plugin name too.

See Qt's tagged [compositor initialization/loader](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/compositor_api/qwaylandcompositor.cpp)
and [DMA-BUF device initialization](https://github.com/qt/qtwayland/blob/v6.12.0/src/hardwareintegration/compositor/linux-dmabuf-v1/linuxdmabufclientbufferintegration.cpp).
No Qt/system library patch is installed. We do not globally force Xwayland
software or disable DRI3 on working accelerated displays; the alternative
[Xwayland -glamor off route](https://man.archlinux.org/man/Xwayland.1.en)
was a diagnostic experiment.

The software route is not a performance, Vulkan/GLX gaming, physical GPU,
suspend/resume or broad browser qualification. The legacy EGL-bind warning may
remain while shared-memory clients work. Real input and inspected pixels, rather
than a ready flag alone, decide whether a measured client works.
