# Build and execution

Run from the repository root. Builds stay here;
test fixtures use owned temporary folders. Per-user deployment is separate from
building; see install/session notes. These commands need no root access and do
not replace the running session.

## Observed dependencies

Installed package versions read on 2026-10-17:

| Package | Version |
| --- | --- |
| qt6-base | 6.12.0-2 |
| qt6-declarative | 6.12.0-1 |
| qt6-wayland | 6.12.0-1 |
| glib2 / GIO | 2.90.1-1 |
| wayland | 1.26.0-1 |
| plasma-integration | 6.7.5-3 |
| cmake | 4.4.4-1 |
| ninja | 1.13.2-3 |
| gcc | 16.2.1+r23+gd564253eb6c8-1 |
| qt6-tools | 6.12.0-1 |
| python | 3.14.8-1 |

CMake requires Qt 6.8+ Core/Gui/Widgets/DBus/Test/Quick/Qml/WaylandCompositor/
Concurrent, GIO, Wayland Server and `wayland-scanner`. Only the installed versions
have been compiled; the declared minimum is not a tested version range. AppMenu
v1 is generated from `/usr/share/qt6/wayland/protocols/appmenu/appmenu.xml`.
KDE platform integration has passed the owned native probe export path. This is
not universal Qt application support.
Python is used by preflight, manifests, qualification-bus setup and safe cleanup.
Native runs also need dbus-run-session. With BUILD_TESTING enabled, shell/native
input injection uses Qt6GuiPrivate headers matching the installed Qt build; production
source uses public APIs and does not link that private module.

## Build and offscreen verification

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Thirteen CTest suites cover 151 local application/policy/shell, desktop-entry, appearance and launcher checks. `native-tests`
and `native-probe` build alongside them but run only through the explicit native
script. Core, Finder/editor and desktop-file workflows have recorded native passes;
the fourth desktop-operation workflow also passed at 1.5x. See status for exact results and source revisions.
The fifth renderer/client teardown workflow also passed in the 04:12 combined gate; its
intentional disconnects and abrupt exit affect only disposable test-owned probes.
The native runner defaults `QTEST_FUNCTION_TIMEOUT` to 30000 milliseconds and
records it in the run manifest. This bounds an individual test's execution;
watchdog failures are not successes or skips.

## Sanitizers

```sh
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-sanitize -j 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize --output-on-failure
```

Leak detection is explicitly disabled because LeakSanitizer cannot operate in
this executor. Run leak detection separately in a capable venue. The full native gate also runs with ASan/UBSan; external browsers and system
libraries remain uninstrumented. qualify-native.sh supplies the matching normal
Qt style plugin to external browsers, records its hash, and requires that build
to exist. Owned instrumented clients keep their instrumented plugin.

## Native tests and interactive prototype

The agent can run automatic checks without touching the live seat:

```sh
python3 scripts/with-headless-display.py --evidence build/artifacts/UNIQUE -- \
  bash scripts/qualify-native.sh
```

The evidence directory must be new. For the instrumented gate, set
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1` and
`FRANKENSTEIN_NATIVE_BUILD_DIR="$PWD/build-sanitize"` before that command.
Run normal and instrumented native gates sequentially.

The complete daily-use gate requires CMake's provenance-recorded X11 bridge
options in **both** build trees (see [X11 integration](x11-integration.md)). A
leftover `xwayland-satellite` file in a build tree is not evidence that its code
was compiled in. The runner rejects a full gate with that option unset; focused
Wayland-only tests remain possible by naming the test. When resuming inside the
installed desktop, start gates with a clean environment to avoid loading that
session's release plugins instead of the build's:

```sh
env -i HOME="$HOME" USER="$USER" PATH=/usr/local/bin:/usr/bin:/bin \
  LANG=en_US.UTF-8 SHELL=/bin/bash XDG_RUNTIME_DIR="/run/user/$(id -u)" \
  python3 -B scripts/with-headless-display.py --evidence build/artifacts/UNIQUE -- \
  bash scripts/qualify-native.sh
```

Agent shell tools must use `login:false`; the user's protected login profile
contains a console-rotation sudo command that must not run in unattended tests.

In a normal graphical user terminal that permits local Unix sockets:

```sh
bash scripts/qualify-native.sh
```

This opens nested test windows with disposable probes and owned Finder/editor
programs using temporary documents. The qualification bus deliberately has no
service activation directories and does not exercise portals or host services. Results go to
`build/artifacts/native-runs/<run>/native-tests.txt`, with a manifest and separate
test/cleanup status. A venue failure exits 77; an assertion failure
is a failed test. It requires a working parent display and OpenGL-capable Qt Quick
renderer. The current Wayland/OpenGL startup selects the installed linux-dmabuf-v1
compositor plugin when no override exists. The gate exercises native rendering
at 1.5x; exact client acceleration, other displays and every buffer path remain
unqualified. Explicit graphics overrides and software previews remain independent.

To inspect the interactive prototype separately:

```sh
bash scripts/run-nested.sh
```

For a single qualification-then-play command, use `bash scripts/play.sh`. It stops
on a failed automatic gate, then opens the interactive desktop after success.
See the [hands-on guide](try-it.md). Application discovery can be restricted to
explicit metadata roots with repeated `--applications-dir /absolute/path` options.

This creates project-local `var/workspace`, `var/workspace/Desktop`, `var/profile`
and a disposable `/tmp/frankenstein-runtime.*` with an activation-free private bus.
The outer window uses the existing display; children use the private
one. No KDE replacement, login-session installation or system power command is
performed. The environment marker is a launcher guard, not authentication.
Applications retain normal user filesystem access: nesting is not confinement.
The manager removes inherited host `DISPLAY` for unsupported X11 clients.
Neither launcher currently supplies real portal backends or automatic service
activation. Application categories that require them still need integration.

Files in `var/workspace/Desktop` appear on the desktop. Open Desktop Folder in its
File menu exposes that directory in Finder. Select icons, Shift-click to extend
selection, drag to place them, and use Open or Command-O for supported folders/text
documents. The desktop File menu also creates folders, renames, duplicates and
moves selected real entries to Trash. Internal folder drops move; Alt-drops copy.
Escape cancels icon dragging. Double-click Trash and use Finder Put Away to restore.
Positions are stored under `var/profile/desktop.json`. These operations passed
their narrow native gate; these are provisional native contracts, not historically
certified behavior. F2 rename, Ctrl equivalents and extension-preserving duplicate
names are deliberate initial adaptations.

Apple menu → Appearance controls WindowShade animation duration and strip
visibility. The initial strip's handle folds it or drags it; its other modules
activate Finder and open Applications/Appearance. Settings live in
var/profile/appearance.json. These controls never change KDE's settings.
Charcoal is used when already installed, with a DejaVu fallback. The optional
local rainbow menu PNG is read directly; see the [hands-on guide](try-it.md).

Use disposable files until qualified. End Nested Session requests closure and
honors retained windows, but this is not a full session manager. Killing the
compositor or using `--exit-after` can disconnect clients and is unsuitable for
real unsaved documents.

Server-free shell rendering:

```sh
mkdir -p build/artifacts
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/frankenstein \
  --preview --exit-after 900 --screenshot build/artifacts/desktop.png
```

The preview cannot launch clients. Its geometry is provisional, not pixel-
certified Mac OS 9.2.2. No proprietary assets are needed.

The seventh native workflow uses actual Finder icon/list views: right-click a
background folder, drag to the desktop, Option-copy, Escape-cancel, preserve a
name collision, move between folder windows and duplicate from the desktop
context menu. This workflow and the eighth desktop-to-folder-window workflow passed in psn_tch's
graphical terminal. The eighth now also checks marquee selection and a group
transfer; that extension passed the 11:09 complete native run.
Native execution remains unavailable in the socket-restricted executor. The private `frankenstein-drop-receiver` executable is a build dependency;
it is not installed or placed on the host desktop.
