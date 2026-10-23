# Native application observations

Keep user observations separate from automated qualification. Opening one
application does not qualify its complete workflow or every application built
with the same toolkit.

## Firefox 158: first context menu can hang (2026-10-20)

The intermittent `firefoxContextMenuSavesImage` first-menu stall also reproduces
on plain headless Sway, without the Platinum compositor. The observations below
locate the stalled main thread in Firefox/libxul; the exact upstream root cause
has not been symbolized or fixed. This release does not claim to fix that stall.

- **Signature** (Firefox `MOZ_LOG=Widget:5,WidgetPopup:5`): `Button 3 press`, then the context
  menu's popup widget is created, and the parent main thread stops after
  `nsWindow::SetInputRegion(0, 4)`. In a good run the next line is `CaptureRollupEvents(1)`.
- **Stack:** the main thread waits in a raw futex from libxul (Rust-style parking). All
  WebRender, style, IPC and Wayland threads are idle; there are no GTK or libwayland frames.
- **Reproduced without this compositor:** plain owned headless Sway, a real right-click via
  wlr-virtual-pointer.
  - 5/150 hangs with a fresh profile;
  - 5/150 with software WebRender;
  - 3/150 with one warm profile.
- **Only the first menu per Firefox process:** 5/200 first menus hung, against 0/195 second
  menus in the same processes.
- **Observed effect:** roughly2–3% in these controlled runs; this is not a general
  failure-rate estimate for every profile, machine or Firefox version. Affected
  processes needed restarting. No second-menu failures were observed in195
  successful first-menu processes; that is bounded evidence, not a guarantee.
- **Gate policy:** identify the recorded press/popup-widget/futex signature before
  attributing a failure to this condition. Preserve failures and retain all
  assertions. A later passing run is not evidence of a production fix.
- **Tools and logs:** `build/artifacts/claude-firefox-stall-hunt-20261020/` (see HANDOFF). Not
  symbolized: that needs Arch debuginfod for libxul, which was not fetched.

## Current update — 2026-10-19

The [normal and instrumented gates](evidence/native-update-20261019/summary.json)
now cover 19 workflows. Disposable Firefox/Chromium save actual image, Blob and
HTTP attachment bytes; Chromium also exercises Save File picker. Pointer-based
Save Image As and fitted/active choosers pass. Firefox exact typing, focus loss/
reacquisition, frame bounds, zoom/shade/close pass. This update is selected for
the next login, without restarting current applications. Earlier Firefox stalls,
Chromium chooser Return, popup keyboard grabs and the real ChatGPT account
workflow remain open; see [browser limits](browser-downloads.md).

The installed compositor now selects linux-dmabuf-v1 on native Wayland/OpenGL
when available and not overridden. A separate [SDL GPU probe](emulators.md) presents verified Radeon-rendered colors
through the installed compositor. Browser acceleration remains unmeasured.
Ymir's native UI launches/closes with an empty disposable profile and software
renderer. The recognized known wrapper translates to the same existing tuned
profile without editing it. This does not certify gameplay, firmware, audio,
controllers, every emulator's settings dialog or X11-only applications.

The entries below preserve earlier qualification and user observations.

## Standalone feedback and keyboard correction — 2026-10-18 local time

psn_tch is now using the standalone session. Editor accepts text; Firefox in the
original installed release did not, including after Ctrl+L. This supersedes any
inference that earlier frame/launch qualification established browser typing.
The regression was reproduced in a disposable Firefox profile with global
add-ons disabled only for the fixture; the user's extensions were not changed.

The installed Qt 6.12.0 compositor sends keyboard events to one `wl_keyboard`
resource. Firefox binds two for the same client/seat. A public-libwayland
observer now mirrors focus, key and modifier events to that client's sibling
resources on the same seat, with resource destruction tracking and an exact
Qt-version guard. No production key contents are logged. Future Qt versions
require a new audit to avoid duplicate delivery after an upstream change.

The [keyboard evidence](evidence/keyboard-broadcast-20261019/20261019T014933-728071/native-tests.txt)
establishes real text entry, focus loss/reacquisition and additional text entry,
alongside the earlier frame/bounds/lifecycle assertions. All ten workflows also
pass in the instrumented run, and nine smoke checks pass against the installed
updated payload. The selected next-login release contains the fix; psn_tch has not
yet restarted to provide physical-session confirmation. Browser media, clipboard,
downloads, IME and general application compatibility remain separate questions.

Kitty 0.49.2 fails against the current inner display: its GLFW backend requests
data-device-manager v3 while Qt advertises v1. GPU/EGL initialization also fails.
An isolated, undeployed backend experiment did not complete its build under
GCC 16's format-truncation warning policy. psn_tch subsequently chose Konsole and
explicitly deprioritized Kitty. No system Kitty files or configuration were
changed, and no Kitty compatibility success is claimed. The Apple menu's
Terminal action already launches Konsole with `--separate`.

## Earlier observations

Current automatic qualification additionally includes a disposable installed
Firefox: [normal frame capture](evidence/20261018T201705-583727/native-firefox-classic-frame.png)
and [complete native sanitizer gate](evidence/20261018T202825-595314/native-tests.txt).
This establishes frame/content bounds, close/zoom/WindowShade and process exit,
not browsing/media/clipboard/download or universal browser compatibility. Both
GTK probes now verify focus, text, button and checkbox state. The user observations
and earlier table below remain distinct from those automatic results.

| Application | Evidence | Established scope | Still to check |
| --- | --- | --- | --- |
| Firefox | psn_tch's 2026-10-18 hands-on report: “firefox opened perfect inside it” | User-observed successful opening inside Platinum Reality | Exact version/build and launch method; browsing and text input; additional windows and dialogs; clipboard; downloads/uploads; audio/video; restart and profile behavior |
| Owned Finder and Editor | Eight-workflow native gate, latest archive `20261018T170917-495823` | Recorded folder/document operations, native input, menus, save/cancel, transfers and lifecycle assertions | Broader filesystem, recovery, accessibility and application interactions |
| Qt test probe | Same native gate | Specific decorations, input, menu, modality and lifecycle assertions | General Qt application compatibility |

The Firefox observation is supplied by the user. No browser process, profile,
history or private browsing data was inspected to record it. The launch planner
provides a native Wayland environment, but this report alone does not establish
the particular browser process/backend or authenticate its executable version.
No new browser execution or automated browser test was performed for this entry.

Prioritize concrete everyday workflows before optional-login-session packaging.
The existing nested launcher permits those hands-on checks without rerunning the
entire qualification gate on every launch. See [hands-on instructions](try-it.md).


Later on 2026-10-18, psn_tch reports the separate-profile styled Firefox “looking
good” and requests classic square title-bar controls. The compositor/profile
adapter for those controls is new work and needs a separate native result.
The preceding [nine-workflow run](evidence/20261018T172632-507871/native-tests.txt)
automatically qualifies the recorded GTK 3/4 probe interactions, not Firefox.
