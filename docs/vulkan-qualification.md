# Vulkan qualification workbench — 2026-10-19

## Update — Claude, 13:00–14:10 UTC: both red leads fixed

Rocky's two open leads were root-caused and fixed with narrow, private
integration changes. No Qt, Mesa, Xwayland or system file changed. The ten
data rows (eight below plus two explicit 16-bit rows) pass in the full gates
listed in HANDOFF.md.

### 1. X11 display loss: Xwayland syncobj release points

- The hung client's stack was symbolized without debug info (Arch debuginfod has
  none for Mesa 26.2.4 or Qt 6.12). Its return addresses were matched to RADV
  call sites at a page-aligned load base. They are `wsi_AcquireNextImageKHR` (it
  builds `VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR` and calls the dispatch
  table), then `wsi_common_acquire_next_image2` (`swapchain->acquire_next_image`,
  checks `VK_SUBOPTIMAL_KHR`), then `x11_acquire_next_image`, then
  `wsi_drm_wait_for_explicit_sync_release` (`drmSyncobjTimelineWait`,
  `WAIT_AVAILABLE`, timeout clamped to INT64_MAX). This is the **explicit-sync**
  acquire, not `vkWaitForFences`. Workbench and disassembly:
  `build/artifacts/claude-x11-syncobj-20261019/`.
- Mesa 26.2.4 source: `wsi_common_x11.c` enables explicit sync when Xwayland
  offers DRI3 1.4 + Present 1.4 + `PresentCapabilitySyncobj`. The WSI event thread
  notices the lost connection, but nothing wakes an acquire already waiting on
  a release point.
- Xwayland 24.1.14 source: `xwl_gbm_supports_syncobjs()` enables DRI3 1.4 from
  kernel and EGL support alone. Without compositor linux-drm-syncobj-v1 (Qt has
  none, so satellite forwards none), `xwl_present_buffer_release()` signals a
  client's release point only when `wl_buffer.release` reaches Xwayland. Once
  Xwayland exits, the point never materializes. On direct Sway, the compositor
  owns the timeline (passthrough) and signals it. That is why the earlier direct
  host comparison exited.
- Causal check (same copied binaries, only a PATH wrapper differs): preloading a
  library that makes Xwayland's `drmSyncobjEventfd` probe fail gave 4/4 passes;
  the control without it gave 3/3 failures. With the fix the client logs
  `QVulkanWindow: Failed to acquire next swapchain image: -1000000000`
  (SURFACE_LOST), then "The X11 connection broke", and exits.
- Fix: the private launcher and preload library in `docs/x11-integration.md`
  ("Implicit sync for X11 clients"). The test now also asserts that Xwayland offers
  DRI3 below 1.4. With the launcher removed it fails with "offered DRI3 '1.4'"
  (run `20261019T134213-144240`).

### 2. Wayland 16-bit formats: advertised but undrawable

- Default-format rows passed exactly when RADV's first offered format was 8-bit
  (VkFormat 43, 44, 50). They failed when it was 16-bit: 97 R16G16B16A16_SFLOAT
  (`20261019T133737-141406`, blank grey content, 990x "Empty shader passed to
  graphics pipeline") and 91 _UNORM earlier. The order varies between runs.
- Qt v6.12.0 `linuxdmabufclientbufferintegration.cpp`: `supportedDrmFormats()`
  returns every EGL-importable format, while `formatFromDrmFormat()` maps only
  8/10-bit RGB(A), 565/4444/1555/332 and YUYV. Anything else gets
  `BufferFormatEgl_Null` and no scene-graph shader.
- New explicit rows `WaylandPrefersFloat16` / `WaylandPrefersUnorm16` reproduce it
  deterministically: before the fix, "Wayland client was offered undrawable VkFormat 97/91"
  (`20261019T134901-147004`). Every Wayland row now asserts that the chosen format
  is not 16 bits per channel (VkFormat 70–97).
- Fix: `src/desktop/dmabuf_formats.cpp`. The plugin resolves
  `eglQueryDmaBufFormatsEXT` through `eglGetProcAddress`, which the compositor
  executable exports (`--export-dynamic-symbol`) and forwards. For that one name
  it returns a filter that keeps exactly the `formatFromDrmFormat()` cases. Gated
  to Qt 6.12.x; other versions log a warning and are left to these rows. The
  clients then fall back to an 8-bit format and draw correctly
  (`20261019T135201-148910`: 10/10 passes).

Still not certified: Steam/Proton/DXVK/VKD3D games, HDR, controllers, audio,
FPS pointer capture and multiple outputs. The sanitizer Vulkan-instance startup
delay is unchanged; the test accounts for it as Rocky arranged.

## Earlier workbench state (Rocky, before the fixes)

**No new release was selected or installed by that earlier work.**
The selected `0.1.0-x11-20261019T113438Z` remains immutable. Its seven installed
ELFs (including style and X11 helper) still match the normal build byte for byte.

## New coverage

`tests/vulkan_probe.cpp` is an owned QVulkanWindow application, available only with
`FRANKENSTEIN_NATIVE_TEST=1`. Vulkan render commands clear distinct green/blue
regions, then red/blue. Async D-Bus inspection reports frame/swapchain counters,
QPA backend and physical device. The test inspects actual composited pixels,
changes the image, resizes, enters/leaves fullscreen and requires normal exit.
A separate data row leaves another GPU client connected through display teardown.
Every client, bus, profile and headless display is disposable; no user game,
Steam account, Wine prefix, live display or input capture is used.

The default-format rows remain active. Two extra SDR rows explicitly prefer
8-bit formats as a controlled comparison; they do not replace default coverage.
The new probe is part of build provenance, not the installed runtime payload.
`vulkan-headers` and `vulkan-icd-loader` are build/test dependencies; no packages
were installed. CMake's Vulkan discovery needs both headers and the loader.

## Observations, not certification

- **Latest corrected six-row runs:** normal `20261019T122031-107576` has
  7 pass / 1 fail / 0 skip (18.865s); sanitized `20261019T122128-108366` has
  the same results (79.848s). Only X11DisplayLoss fails. Both default normal-operation
  rows and both SDR rows pass; Wayland default display loss passes. Init/cleanup
  account for two of seven passes. This remains an overall failing GPU gate.
- Timing `20261019T121806-105156` localized startup: Qt application 68ms,
  Vulkan instance 13,620ms, show 13,664ms, resources 13,754ms, first frame 13,832ms.
  The test now waits separately up to 20s for the flushed show marker, then keeps
  its 8s mapping and all pixel assertions. No production deadline changed.
  Display-loss rows now retain their first verified presenting client, while
  normal rows independently test normal close. One initialization per data row
  keeps the existing 30s watchdog. The corrected runs above validate this change.
- The following are **preserved earlier results**, not the current gate status:

- Normal run `20261019T121233-101043`: 5 QtTest passes, 1 failure, no skips.
  Both Wayland and X11 present real frames, change colour, resize, restore from
  fullscreen and close normally. Wayland connected-display-loss also passes.
  X11 connected-display-loss fails: the client remains in a GPU wait.
- Sanitized `20261019T121312-101745`: 3 passes, 3 failures, no skips. X11 normal
  operation passes; X11 connected-display-loss repeats the wait. Both default
  Wayland rows exceed the map deadline, selecting Vulkan format 91 and submitting
  DRM fourcc `0x38344258` (XB48 / XBGR16161616). Their later mapping does not turn
  those timeouts into passes. No assertion was relaxed and this is not a clean gate.
- SDR comparison: normal `20261019T121552-103326` passes both added cases
  (4 QtTest passes including init/cleanup, no failures/skips). Sanitized
  `20261019T121635-103967` still fails both mapping deadlines at roughly 13s,
  even with format 44 / 8-bit XRGB8888. **This contradicts a format-only
  explanation for those startup delays.** Do not blindly filter formats and call
  the delays fixed. The format mapping mismatch remains a separate source lead.
- All 13 local suites pass in both normal (8.99s) and sanitizer (10.75s) builds.
  These do not substitute for the failing GPU cases. ASan leak detection is off;
  system graphics libraries and Rust bridge are not instrumented.
- Diagnostic `20261019T120533-96808`: owned X11 client's main-thread stack is
  `QVulkanWindowPrivate::beginFrame` → RADV → `drmSyncobjTimelineWait` → ioctl.
  The compositor survives; fixture cleanup kills only the stuck owned client.
- Direct Sway + same bridge/client comparison exits on X11 connection loss;
  `build/artifacts/vulkan-direct-host-baseline-20261019/`. Therefore a universal
  driver-failure explanation is unproven. Sway's protocol/synchronization path
  differs, so this does not isolate one faulty component. Private bus activation
  left `/tmp/platinum-headless.2o3qv0p7` retained; do not recursively delete it.
- Explicit GL completion before teardown (`121040-99862`) did not fix the wait.
  Retiring the Qt clients before stopping the helper (`121134-100451`) did not
  fix it either. Both experiments were fixture-only and removed. Do not ship an
  unproven production teardown reorder or global glFinish workaround.

## Pixel-format lead

Official Qt v6.12.0 source is captured with URLs/hashes under
`build/artifacts/qt-vulkan-format-audit-20261019/`:

- `qtwayland/src/hardwareintegration/compositor/linux-dmabuf-v1/`:
  `supportedDrmFormats()` returns EGL's queried formats without filtering them
  through `formatFromDrmFormat()`. The latter maps 8/10-bit RGB(A) and YUYV, but
  returns Null for XBGR16161616. The local DRM header identifies observed XB48.
- `qtbase/src/gui/vulkan/qvulkanwindow.cpp`: default colour selection takes the
  first supplied format; the application can request preferred formats. The
  observed order differs between runs. Do not infer a stable format preference.
- One early default Wayland run produced empty-shader/blank content. The mismatch
  is a strong lead, not proof that every renderer failure has this same cause.

Primary source locations:
[Qt compositor integration](https://github.com/qt/qtwayland/blob/v6.12.0/src/hardwareintegration/compositor/linux-dmabuf-v1/linuxdmabufclientbufferintegration.cpp),
[Qt Vulkan window](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/vulkan/qvulkanwindow.cpp),
[Vulkan WSI specification](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html).
The Vulkan spec discusses infinite acquisition waits and surface loss; it is not
permission to declare our observed stuck client correct or harmless.

## Next engineering steps

Items 1–3 below were completed by the 13:00–14:10 UTC update above. Item 4's
relative pointer and constraints work is next.

1. (Done.) Diagnose the remaining X11 display-loss GPU wait with the owned-client stack
   and a smaller same-format/same-protocol comparison. The sanitizer startup
   accounting is corrected and verified; do not spend another turn treating it as
   a compositor mapping failure. Retain the earlier evidence and active exit check.
2. Add a controlled explicit 16-bit-format client, capturing material readiness
   and pixel results. Default format order varies, so passing defaults alone do not
   settle this. Check whether unsupported advertised formats are the
   mapping/blank-window cause. Prefer a narrow, versioned, private integration fix
   over modifying system Qt or globally forcing application preferences. Any fork
   needs source/license/provenance, a clear update strategy and full release gates.
3. Independently diagnose X11 display-loss synchronization. Compare identical
   pixel formats and protocol capabilities with direct Sway. Keep renderer joining
   before protocol owner destruction; do not regress the earlier cleanup crashes.
4. Resolve required cases before calling Vulkan qualified. Then implement proper
   host relative-pointer/constraints forwarding with an emergency release path.
   No synthetic absolute-position deltas and no protocol advertisement without
   real host support. See the ordered plan at the top of `HANDOFF.md`.

No Steam/Proton/DXVK/VKD3D game, controller, audio, multi-output, physical-seat or
FPS mouse-capture compatibility claim follows from this fixture.

Durable continuation package: `docs/evidence/vulkan-qualification-20261019/`,
including current test-source snapshot and exact earlier pre-run manifests. The
normal and sanitized source snapshots/binaries evolved during investigation;
do not describe earlier hashes as identical to the final source revision.
The package's `summary.json` verifies the selected runtime binaries and protected
files remain unchanged. No new release or full-suite qualification is claimed.
