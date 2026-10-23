# Platinum Reality — development notes (the original README)

Native Arch Linux desktop research and implementation · development version 0.1.0

Reproduce the Mac OS 9.2.2 interaction model with native Linux applications.
Original Macintosh binaries and emulation are outside scope. This project is
developed in the separate `frankenstein-macintosh-native` working directory.
The earlier KDE Platinum theme repositories, their v0.2.0 scope and Potato
remain protected. “Frankenstein Macintosh” is the earlier experiment name; the
product name is now **Platinum Reality**, as requested.

The [source repository](https://github.com/psn-tch/platinum-reality) is private
during development. Build outputs, personal assets/profiles, the local handoff
and large qualification archives are excluded. Links into `docs/evidence/`
resolve on the development machine; they are not included in the Git snapshot.

[Bluetooth in Applications](docs/bluetooth.md) opens the same panel as the Control
Strip: Alt+D, search Bluetooth, then Return. The qualified release
`0.1.0-touchscreen-20261021T124926Z` is selected for the next user-chosen
login. It also includes [Sound](docs/sound.md), [alert sounds](docs/alert-sounds.md),
modeless Get Info windows and the [saved-sign-ins integration](docs/saved-signins.md).
Real wallet unlock/sign-in retention and physical audio still need user trials.

Closed X11 games now leave the Application menu after their last mapped window
closes; native windowless applications remain registered. Unknown Steam IDs use
the supplied window title when available. [Lifecycle evidence](docs/evidence/application-lifecycle-update-20261021/summary.json)
uses owned probes, not actual Steam/game execution.

[Touchscreen delivery](docs/touchscreen.md) now reaches every same-client/seat
binding, fixing reproduced Firefox taps and page swipes. Mouse workflows remain
covered. Physical panel calibration and broad X11 touch support remain user trials.
The extra rotated2.3x checks passed; two earlier sanitizer Firefox-exit deadline
failures are preserved and their cause remains unresolved.

**This is a working development desktop, not a finished release.** The desktop
runtime passed all 26 local suites and 56 native test cases (including data rows,
initialization and cleanup) in both normal and ASan/UBSan builds. Coverage includes
browser saves and typing, Finder/editor operations, file transfers, shortcuts,
Puzzle, screenshots, X11, GPU presentation and pointer capture. Leak detection is
disabled; external programs and system libraries are not instrumented. See the
[current release ledger and limits](docs/status.md).

The newer [Mirror pointer/cursor experiment](docs/mirror-cursor-development.md)
has separate development evidence: 15 two-monitor checks and four ordinary
pointer regressions per build. Its cursor component is compiled only into the
opt-in test target. This is not production Mirror or a newly installed release.

Native tests run automatically on an owned headless Sway display, without taking
the user's VT or changing the live desktop. The native compositor now selects
Qt's installed linux-dmabuf-v1 integration on Wayland/OpenGL when available.
This avoids the legacy EGL-bind initialization failure; it does not certify
all GPU applications or establish the cause of earlier intermittent Firefox stalls.

The exact selected user-local release and installed/session qualification counts
are recorded in [the release ledger](docs/status.md). The
[Sway-backed session supervisor](docs/standalone-session.md) provides seat and
lock duties; Platinum Reality owns the visible desktop. Qualified updates take
effect at a user-chosen logout/login. The fingerprint gate and KDE fallback are
retained: press K at the five-second login selector, or use
`~/.local/bin/platinum-dreams-use-kde` to change the saved default.
No root package install or protected-project modification is involved. Physical
authentication, suspend and broad application compatibility remain unqualified.
Extend is opt-in; production Mirror is unfinished. See
[multi-display development](docs/multi-display-development.md).

Historical appearance and useful modern interactions are both project goals.
See [modern conveniences](docs/modern-conveniences.md) for scope and current limits.

## Implemented source

- Alt+D opens Applications; Alt+Q requests a normal application close, preserving
  Save/Cancel; Alt+Shift+S opens the crop/save/cancel screenshot tool.
- [Desktop Picture](docs/desktop-picture.md) provides preview, Fill/Fit/Center/Tile
  and saved choices. The personal wallpaper remains external to the package.
- Apple-menu Puzzle implements a solvable 15-tile game, mouse/arrows, Shuffle and
  move counts. Arrow shortcuts work with Shuffle focused; Tab/Space remain usable.
- [Control Strip quick controls](docs/quick-controls.md) provide asynchronous
  volume/mute, active-display brightness, IWD/NetworkManager Wi-Fi and BlueZ.
  Unsupported devices are disabled. Automatic mutation tests use fake devices.
- [Bluetooth](docs/bluetooth.md) lists devices paired under KDE, with connection
  and battery status, Connect/Disconnect, Add Device and Platinum pairing alerts.
  The qualified update is selected for the next login; real-device behavior
  still needs a hardware trial.
- [Browser saving](docs/browser-downloads.md): popup clicks retain parent-window
  activation. Disposable Firefox/Chromium fixtures save actual image, Blob and
  attachment bytes; Chromium also uses its Save File picker. The real ChatGPT
  account workflow still needs hands-on confirmation.
- Existing-settings integration for recognized emulator entries, including the
  known tuned Ymir wrapper. A disposable native Ymir UI launch passes; gameplay,
  audio, controllers and other emulators are not generally qualified.
- File drags from Finder icon/list views to other folder windows and a private
  desktop drop endpoint. Default move, Option/Alt or Ctrl to copy, Escape to cancel,
  desktop folder/Trash targets, collision preservation and partial-job reporting.
  Desktop icons also drop directly into owned open folder windows and child
  folders, matched by live surface identity. These paths passed the native gate;
  ordinary-application DnD is not universally qualified.
- Hold left-click and drag across empty desktop or Finder space to select a
  group. Desktop Shift/Ctrl-drag adds to selection; Escape restores the prior
  group. Dragging an already-selected desktop item carries the group.
- Platinum right-click menus on files and empty space in Finder and the desktop.
  File actions include Open, Open With, Info, Rename, Duplicate and Trash; background
  actions include New Folder and view/arrangement choices. Existing groups survive
  right-clicking a selected item. No placeholder Cut/Copy/Paste commands are shown.

- Animated WindowShade with retained full-size client content, input/focus guards
  during motion, and an Appearance speed slider (instant–1.2 seconds), presets and
  a working preview. The initial Control Strip opens Finder, Applications and
  Appearance; it folds, drags and remembers its state in the project profile.
- Charcoal UI when installed, plus the local rainbow Apple menu icon. These
  personal assets are read from their existing locations, never bundled or installed.
- Searchable Applications browser, explicit desktop-entry argument expansion and
  profile-local Open With choices. Finder document opening goes through a narrow
  private-bus bridge that checks the live sender executable and workspace path.
  Workspace `.desktop` files open as text, not as executable shortcuts.
- A build-local Qt Widgets style plugin shares Platinum colors, Charcoal, beveled
  controls and scrollbars with compatible Qt 6 programs launched here. Profile-local
  CSS styles GTK 3/4. Installed launches use normal application settings and the
  normal Firefox profile; isolated profiles remain available for disposable tests.
  Firefox receives compositor-owned Platinum frame controls in either mode.
  [App styling](docs/application-appearance.md) records what is checked and pending.
  Client-side controls stay available; missing menu exports remain a limitation.
  A separate [decoration experiment](docs/firefox-decoration-development.md)
  checks Firefox captions and GTK headerbars; it is not in the selected release.
- Owned Editor Save As uses an in-session Widgets picker. Unsupported UTF-8/binary
  input is rejected with its original save path cleared.
- C++20 application/window policy: activation, hiding, group ordering,
  WindowShade, menu context, transient relationships and modal blocking.
- Qt Wayland Compositor adapter and Qt Quick shell: server decorations,
  application switching, client launch and private-session menu integration.
  The core native probe workflow has passed; broader application support remains unqualified.
- Modal alerts retain their application's authenticated menu, with dispatch blocked
  and prior action snapshots retired. Fixed-size windows have no grow/zoom controls;
  resizable modal pickers retain resizing. Owned alerts use original monochrome artwork.
- Real desktop entries from the workspace's `Desktop` folder: selection, group
  movement, remembered positions, filesystem updates, folder/text opening and
  desktop-owned keyboard/menu focus. New Folder, rename, duplicate, recoverable
  Trash and internal folder drops now use asynchronous file jobs. Shell dialogs
  reserve input and session exit waits for jobs. The combined native gate passed
  this slice, including Finder Put Away and untouched background documents.
- Owned Qt Widgets Finder: spatial folder-window reuse, icon/list views,
  remembered geometry/view/icon positions, native file operations, Trash and
  Put Away. Private-bus cross-process folder reuse passed the native workflow.
- Owned UTF-8 editor: atomic saving, editing actions, Command/Meta and Ctrl
  shortcuts, Save/Discard/Cancel close behavior. Native modifier forwarding needs
  a Qt 6.12-specific adaptation; its current live result is recorded in status.
- Bounded DBusMenu decoder and async broker: owner/PID checks, endpoint
  generations, snapshot revisions and stale-action rejection. This is a narrow
  Qt export integration, not universal Linux menu support.

Read [architecture](docs/architecture.md), [status](docs/status.md),
[acceptance](docs/prototype-acceptance.md), [historical claims](docs/historical-claims.md),
[build instructions](docs/build-and-run.md), [provenance](docs/source-manifest.md)
and [roadmap](docs/roadmap.md).

For hands-on use, follow the [first play session](docs/try-it.md). Its launcher
runs the automatic native checks before opening the interactive desktop:

```bash
bash scripts/play.sh
```

## Build and verify

Existing dependencies were used; nothing was installed system-wide.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Render the shell without a display server or client launches:

```sh
mkdir -p build/artifacts
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/frankenstein \
  --preview --exit-after 900 --screenshot build/artifacts/desktop.png
```

In a normal graphical user terminal with Unix sockets enabled:

```sh
bash scripts/qualify-native.sh
bash scripts/run-nested.sh
```

Native qualification is separate from the 13 local CTest suites. IPC
venue failure returns 77, not success. A nested session uses private D-Bus,
runtime, profile and workspace; it does not replace KDE or install a login
session. Nesting is not a security sandbox.

## Authorization and limits

psn_tch expanded the source-only authorization to implementation, builds and tests,
and authorized native implementation, automatic tests and per-user desktop deployment.
The former executor IPC restriction is resolved. No root actions, system package
installation, repository initialization, commits or pushes occurred. No Apple
software or assets are bundled. No existing KDE Platinum Reality repository or Potato files were changed.

No complete historical certification, universal menu/CSD adaptation, Xwayland
integration or production session safety is claimed. Project licensing remains
undecided; Qt Wayland Compositor has GPL/commercial terms to resolve before
redistribution.


The earlier browser offset shown in `~/o/issue1.png` is corrected. The owned
Firefox profile uses its tab strip with no duplicate caption buttons; GTK shadows
are removed, and child-surface sizes/positions retain fractional precision.
The native regression checks the root and visible valid child-surface bounds
before capture and after zoom/restore. Expanded native popups remain unclipped.
