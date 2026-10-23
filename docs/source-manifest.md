# Source and licensing manifest

## Clipboard integration candidate — 2026-10-21

The clipboard and Command-key adapter is original project source and uses
private-but-exported Qt 6.12.0 request tables and keyboard state. The exact-version
guard requires a new audit on Qt updates and does not promise that newer private
symbols will link. No system library is modified or upstream implementation
source vendored. The tagged upstream references and independently reproduced
integration defects are recorded in [the clipboard review](clipboard-integration.md).
The retained-transfer cache remains unbounded by this adapter; image/text
preview budgets do not establish a sandbox. Historical claims from the private
VM observations are attributed to Sammy and were not independently authenticated
during integration. No Apple application, font or artwork is included.

## Touchscreen and private source repository — 2026-10-21

The selected touchscreen adapter uses Qt6.12 private touch-resource access in
client_surface_item.cpp, alongside its version-guarded pointer workaround. It
was designed after the exact tagged [QWaylandTouch source](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/compositor_api/qwaylandtouch.cpp)
and owned Firefox protocol traces showed multiple bindings with single-resource
delivery. Production sends mapped contacts to every resource for that client/seat;
no upstream implementation file or system library was replaced. Other Qt versions
require independent review. See [touchscreen evidence and limits](touchscreen.md).

At the owner's request, source was uploaded to a private GitHub repository and
replaced the earlier KDE project's current files through a normal commit. The old
KDE history/archive tag is retained; its prior licensing remains historical. No
new project source license has been selected. No personal assets, profiles, local
research evidence or binaries were uploaded; no public native release was made.

The [Firefox decoration experiment](firefox-decoration-development.md) uses a
separate local harness and an upstream protocol XML with its LGPL attribution.
That adapter and launch-setting trial are not integrated or shipped.

## Native update — 2026-10-19

New original source implements quick-control helpers/bridge/QML, Puzzle, screenshot
crop/save and local Desktop Picture selection. No historical Apple binary, artwork,
font or wallpaper bytes are bundled. Quick controls use already-installed tools
with fixed argument vectors and ordinary user permissions. Existing emulator
wrapper/settings paths are preserved; no ROM or firmware is acquired or run.

The graphics preference selects Qt's installed linux-dmabuf-v1 server plugin via
its documented environment integration hook after reading the tagged
[compositor loader](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandcompositor.cpp)
and [DMA-BUF integration](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/hardwareintegration/compositor/linux-dmabuf-v1/linuxdmabufclientbufferintegration.cpp).
No Qt source is copied or system library patched. Qt version-specific pointer/
focus observations and popup-grab limits are described in browser-downloads.md.

The [current ledger](evidence/native-update-20261019/summary.json) records exact
pre-run source/build inputs, instrumented/normal results and external style hashes.
External uninstrumented browsers use the normal style plugin during ASan tests;
owned instrumented applications retain the instrumented one. This avoids loading
an ASan plugin into an ordinary Chromium process without disabling sanitizer
checks. No third-party browser patch or extension change was installed.

## Standalone keyboard correction and desktop pictures

The keyboard adapter was designed after inspecting the tagged
[Qt 6.12.0 keyboard implementation](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandkeyboard.cpp).
It uses public libwayland protocol observers/resource destruction listeners and
public Qt seat/surface APIs; no Qt implementation code or private production
headers were copied. The exact-version guard requires review on a Qt update.
The new native fixture types only a controlled local-page phrase. Test-profile
add-on isolation does not modify the user's browser preferences or extensions.

The original desktop-picture chooser uses installed Qt image decoders and
project-owned QML controls. It stores local paths and placement only; picture
bytes, Apple artwork and Charcoal remain outside the package.

An isolated Kitty 0.49.2 source archive and its attempted private Wayland patch
remain under ignored `build/third-party/kitty-0.49.2/`, with URL and SHA-256 in
`provenance.json`. The archive's GPLv3 and GLFW notices were read. The build
failed before producing a usable backend; nothing was installed or integrated.
The experiment was stopped at the user's request to prioritize Konsole and
desktop development. It is not a project dependency or redistribution artifact.

## Latest additions: personal install preview and geometry correction

The original launcher, Arch recipe, source snapshotter, staged-install checks and
application icon are project source. Snapshot/package inputs exclude profiles,
build outputs, research evidence, Apple assets, Charcoal and wallpaper bytes.
Project source licensing remains undecided; the private package development
notice grants no public redistribution license. System libraries remain external
dependencies. No system package installation is part of makepkg qualification.

Subsurface precision work read the tag-labelled [Qt 6.12.0 item implementation](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandquickitem.cpp)
and [xdg integration implementation](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/extensions/qwaylandxdgshellintegration.cpp).
Those sources explain implicit integer dimensions and default child positioning;
the project uses public APIs, not copied Qt implementation or private production
headers. Browser tab-titlebar preference semantics were checked against [Mozilla's support discussion](https://support.mozilla.org/en-US/questions/1444123);
actual installed-browser behavior is established only by the recorded native
tests and inspected captures. This does not authenticate all Firefox components.

The later ten-workflow native ASan/UBSan run passes; leak detection is disabled,
and system Firefox/libraries are not instrumented. Earlier entries below claiming
no agent browser execution or no native sanitizer execution describe prior phases.
No host Firefox profile or browsing history was imported or inspected.

The file-drag work additionally inspected installed Qt 6.12 public
`qwaylanddrag.h`, `qwaylandseat.h`, `qwaylandquickitem.h` and
`qwaylandquickoutput.h`. Production uses those public APIs. The Qt 6.10.0
upstream implementation rendered by Code Browser was read as an older explanatory
reference: [Quick drag routing](https://codebrowser.dev/qt6/qtwayland/src/compositor/compositor_api/qwaylandquickitem.cpp.html)
and [data-device targeting](https://codebrowser.dev/qt6/qtwayland/src/compositor/wayland_wrapper/qwldatadevice.cpp.html).
Those generated source pages are not the exact installed 6.12 implementation;
the new native workflow must verify the current transfer behavior. No Qt source
was vendored and no private compositor headers were added to production.

Research accessed 2026-10-17. This file records inspected references, not a vendor
tree, package lock, downloaded source archive or cryptographic authentication.
The C++/QML project source is original; project licensing remains undecided.
Build directories contain generated AppMenu bindings from installed Qt XML,
whose LGPL-2.1-or-later notice is retained by wayland-scanner. Those generated
files are distinct from original source and are not vendored here. The initial
River investigation below is preserved as historical architecture research;
the executable now uses installed Qt Wayland Compositor. See the current
dependency inventory at the end of this document.

## Pinned River evidence

Commit: [`3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5`](https://github.com/riverwm/river/commit/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5).
This identifies the inspected baseline, not the latest upstream state.

| ID | Exact source | Status / use | License evidence |
| --- | --- | --- | --- |
| R01 | [protocol/river-window-management-v1.xml](https://raw.githubusercontent.com/riverwm/river/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5/protocol/river-window-management-v1.xml) | Readable, interface version 5; management boundary | MIT header |
| R02 | [river/WindowManager.zig](https://raw.githubusercontent.com/riverwm/river/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5/river/WindowManager.zig) | Readable, version-5 global; sequence/lifetime audit | GPL-3.0-only header |
| R03 | [river/Window.zig](https://raw.githubusercontent.com/riverwm/river/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5/river/Window.zig) | Readable; visibility, configuration and representation state | GPL-3.0-only header |
| R04 | `river/Server.zig` at same commit | Exact raw/blob requests unavailable; pinned contents not inspected | Not independently established for unreadable bytes |
| R05 | `river/Seat.zig` at same commit | Exact raw/blob requests unavailable; pinned contents not inspected | Same limitation |
| R06 | `river/XdgToplevel.zig` at same commit | Exact raw/blob requests unavailable; pinned contents not inspected | Same limitation |
| R07 | `river/XwaylandWindow.zig` at same commit | Exact raw/blob requests unavailable; pinned contents not inspected | Same limitation |

## Mutable and documentation references

Branch URLs were read through the research tool and can change. They are not
silently promoted to the pinned River tree. The source response originals were
not saved or hashed; no fixity claim is made.

| ID | Location | Use / limitation |
| --- | --- | --- |
| M01 | [River main protocol](https://raw.githubusercontent.com/riverwm/river/main/protocol/river-window-management-v1.xml) | Returned version 6; differs from selected baseline |
| M02 | [River main Server](https://raw.githubusercontent.com/riverwm/river/main/river/Server.zig) | Reviewed global setup; no native AppMenu/GTK association route identified there |
| M03 | [River main Seat](https://raw.githubusercontent.com/riverwm/river/main/river/Seat.zig) | Input/focus investigation; exact baseline equivalence unknown |
| M04 | [River main XdgToplevel](https://raw.githubusercontent.com/riverwm/river/main/river/XdgToplevel.zig) | Native-client metadata investigation |
| M05 | [River main XwaylandWindow](https://raw.githubusercontent.com/riverwm/river/main/river/XwaylandWindow.zig) | XWM correlation and activation investigation |
| M06 | [River project](https://github.com/riverwm/river) | Current architecture, source/protocol license summary, contribution policy |
| M07 | [Author-hosted manager protocol](https://isaacfreund.com/docs/wayland/river-window-management-v1/) | Prior research returned version 5; documentation is not an exact source lock |
| Q01 | [Qt QMenuBar](https://doc.qt.io/qt-6/qmenubar.html) | Linux global-menu support/opt-out; served docs identify Qt 6.12.0, not installed version |
| Q02 | [KDE Qt-6 platform theme](https://raw.githubusercontent.com/KDE/plasma-integration/master/qt6/src/platformtheme/kdeplatformtheme.cpp) | Menu integration entry point; mutable, LGPL alternatives in header |
| Q03 | [KDE Qt-6 Wayland helper](https://raw.githubusercontent.com/KDE/plasma-integration/master/qt6/src/platformtheme/kwaylandintegration.cpp) | Reviewed current helper; not proof of complete Qt export path |
| Q04 | [KDE Qt-6 X11 helper](https://raw.githubusercontent.com/KDE/plasma-integration/master/qt6/src/platformtheme/x11integration.cpp) | Reviewed current helper; do not infer old Qt-5 behavior |
| Q05 | [KDE AppMenu XML](https://raw.githubusercontent.com/KDE/plasma-wayland-protocols/master/src/protocols/appmenu.xml) | Surface-to-menu endpoint contract; exact commit still unpinned |
| Q06 | [AppMenu registrar XML](https://raw.githubusercontent.com/KDE/plasma-workspace/master/appmenu/com.canonical.AppMenu.Registrar.xml) | X11 window registration contract; mutable |
| G01 | [GTK shell XML](https://raw.githubusercontent.com/GNOME/gtk/main/gdk/wayland/protocol/gtk-shell.xml) | Surface D-Bus metadata and capabilities; exact commit still unpinned |
| G02 | [GTK Application.set_menubar](https://docs.gtk.org/gtk4/method.Application.set_menubar.html) | Optional menu model; not every application exports a full menubar |
| G03 | [GIO DBusMenuModel](https://docs.gtk.org/gio/class.DBusMenuModel.html) | Proxy for exported model; served library docs identify 2.91.0, not installed version |
| G04 | [GIO DBusActionGroup](https://docs.gtk.org/gio/class.DBusActionGroup.html) | Proxy for exported actions |
| E01 | [Electron Menu](https://www.electronjs.org/docs/latest/api/menu) | Runtime/application menu contract; latest docs not a compatibility result |
| E02 | [Chromium AppMenu source, commit 9c30fb37950c6a0a7ab2875f38ca3953e27963ae](https://chromium.googlesource.com/chromium/src/%2B/9c30fb37950c6a0a7ab2875f38ca3953e27963ae/ui/ozone/platform/wayland/host/org_kde_kwin_appmenu.cc) | Prior pinned implementation evidence; no Electron build qualified |
| N01 | [Wayland Xwayland documentation](https://wayland.freedesktop.org/docs/book/Xwayland.html) | Rootless XWM ownership and X11 trust boundary |
| N02 | [FileChooser portal backend](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.impl.portal.FileChooser.html) | Future owned picker contract |
| N03 | [GIO File](https://docs.gtk.org/gio/iface.File.html) | Initial candidate, subsequently linked and exercised by native file operations |
| N04 | [Trash specification](https://specifications.freedesktop.org/trash/latest/) | Future native storage interoperability |
| N05 | [Qt licensing guidance](https://www.qt.io/development/open-source-lgpl-obligations) | Module-specific obligations; not project license selection |

Some references above were inspected in the preceding architecture research and
retained here; no new functional qualification is implied. APIs from development
documentation must be checked against a chosen supported dependency release.

## Historical sources

Read-only collection: Scholar README (private research notes),
bibliography (private research notes),
Phase 2 (private research notes),
Phase 3 (private research notes).
These are referenced in place. No original collection file is copied, altered,
rehash-certified, or treated as an authenticated target Help corpus by this phase.

Original Apple anchors: [TN1176, 9.0](https://developer.apple.com/library/archive/technotes/tn/tn1176.html)
and [TN2010, 9.1](https://developer.apple.com/library/archive/technotes/tn/tn2010.html).
Their release limits survive reuse; inheritance to 9.2.2 remains claim-specific.

## Acquisition and licensing disposition

GitHub commit API requests were unavailable through the browser research tool.
The River commit was identified through its readable history/commit page, then
the three exact source URLs above were opened successfully. No checkout, archive,
package or proprietary payload was downloaded into this project.

The River acquisition gap remains open; no River build is claimed. The Qt
prototype uses installed development files instead. No Apple ROM, system
library, artwork, font or sound payload is bundled. The current UI can read
personal installed typography/menu artwork as documented below. Preserve dependency
licenses/notices before reuse or distribution; this is not legal clearance.

## Current Qt prototype inventory

Observed installed versions are recorded in [build instructions](build-and-run.md).
The declared minimum Qt version has not been separately tested. These sources
were read locally and linked by the build, not acquired as upstream source trees:

| Input | Exact local location / use | Provenance and licensing status |
| --- | --- | --- |
| Qt Wayland Compositor 6.12.0 | `/usr/include/qt6/QtWaylandCompositor/` and installed library | Public headers identify `LicenseRef-Qt-Commercial OR GPL-3.0-only`; [Qt's module licensing documentation](https://doc.qt.io/qt-6/qtwaylandcompositor-index.html#licenses-and-attributions) confirms the compositor/integration-plugin distinction |
| Qt Core/Gui/Widgets/Quick/Qml/DBus/Test/Concurrent 6.12.0 | Installed development headers and dynamic libraries | Module-specific Qt terms; no project-wide license selected |
| AppMenu XML v1 | `/usr/share/qt6/wayland/protocols/appmenu/appmenu.xml` | David Edmundson 2017; `LGPL-2.1-or-later`; generated only under build directories; not the mutable upstream v2 |
| Wayland Server 1.26.0 | Installed public headers/library and scanner | Linked protocol infrastructure; retain applicable upstream notices for redistribution |
| GIO 2.90.1 | Installed GLib/GIO headers/library | Native file backend; no source copied into project; complete distribution notice audit remains pending |
| KDE platform integration 6.7.5 | Installed Qt platform-theme plugin | Owned-client menu export passed recorded native workflows; broader compatibility remains unqualified |
| DejaVu Sans / Sans Mono | Installed font-family lookup only | No font files bundled; no Apple typography claim |
| Charcoal | Existing ~/.local/share/fonts/Charcoal.ttf, requested through QFontDatabase when available | User-installed face; no font bytes acquired, copied, installed or licensed by this project; provenance of the converted font is not authenticated |
| Local rainbow Apple menu image | Existing ~/.local/share/icons/NineIcons/places/16/distributor-logo.png, or a user-supplied profile/assets/apple-menu.png | Installed PNG inspected visually and read at runtime; no payload copied; not authenticated as an original OS 9.2.2 resource |

Primary current API references include [Qt compositor](https://doc.qt.io/qt-6/qwaylandcompositor.html),
[xdg-shell](https://doc.qt.io/qt-6/qwaylandxdgshell.html),
[shell surface item](https://doc.qt.io/qt-6/qwaylandquickshellsurfaceitem.html)
and [Wayland item](https://doc.qt.io/qt-6.12/qwaylandquickitem.html).
Local installed headers, rather than mutable online versions, determined the
compiled API calls. Full upstream source audits, dependency fixity, package
reproducibility and redistribution policy remain open. Nothing has been published.

## Native refinements observed 2026-10-18

- [Qt 6.12.0 Wayland item source](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandquickitem.cpp),
  `updateSize`, `scaleFactor`, `mapToSurface` and `mapFromSurface`: explains the
  original DPR mismatch. The logical-space correction has a live 1.5x geometry
  and far-edge-input result, independent of claims about other outputs.
- [Qt development keyboard source](https://raw.githubusercontent.com/qt/qtwayland/dev/src/compositor/compositor_api/qwaylandkeyboard.cpp)
  and [compositor event handler](https://raw.githubusercontent.com/qt/qtwayland/dev/src/compositor/compositor_api/qwaylandcompositor.cpp):
  modifier repair omits Meta while the native event handler maintains XKB state.
  These are **mutable development references**, not an authenticated exact 6.12
  implementation checkout. Installed 6.12 private-header state declarations and
  observed native failures motivated a version-bounded public-API adapter.
- Installed `QtGui/6.12.0/QtGui/qpa/qwindowsysteminterface.h` is used **only by
  shell/native test executables** to inject complete native events through the
  window-system handler. This test dependency is tied to its Qt build; the
  production compositor does not link QtGuiPrivate or private keyboard APIs.
- The custom qualification-bus policy follows the locally installed
  `dbus-daemon(1)` configuration contract. It contains no activation directories
  or host includes. Native cleanup success and empty stderr are recorded in the
  run evidence; no host D-Bus configuration was changed.

Run manifests pin the original project inputs and binaries before execution.
The 00:29 initial run has only an after-run record; later runs have pre-run hashes.
No evidence file is promoted to original Mac OS 9.2.2 behavioral certification.

The desktop entry model uses the installed Linux statx ABI (`sys/stat.h`) for
device/inode/birth identity, with a name fallback. These are native presentation
aids, not Macintosh file IDs or credentials. DesktopIcon.qml uses original drawings.

The resize investigation consulted installed public compositor headers and
[Qt's development compositor source](https://raw.githubusercontent.com/qt/qtwayland/dev/src/compositor/compositor_api/qwaylandcompositor.cpp),
`init`/`processWaylandEvents`: event-loop blocking triggers dispatch/output flushing.
The adapter queues `wl_display_flush_clients` without nested request dispatch.
[Qt's development xdg-shell source](https://raw.githubusercontent.com/qt/qtwayland/dev/src/compositor/extensions/qwaylandxdgshell.cpp)
distinguishes acknowledged states from sent configures. These mutable references
are diagnostic evidence, not authenticated exact installed source bytes.

## Dialog and renderer refinements observed 2026-10-18

Installed public `qwaylandxdgshell.h` supplies modality and minimum/maximum-size
properties; their signals now update native chrome capabilities. Parent menu
resolution still uses the existing surface endpoint plus strict D-Bus owner/PID
checks, with a separate active/modal context revision. No Apple dialog or menu
implementation was copied. Monochrome alert artwork is original QPainter geometry
with installed DejaVu glyphs; dialog buttons request no host-theme icons.

Installed public `qquickwindow.h` supplies persistence flags and releaseResources;
`qwaylandcompositor.h` supplies clients/destroyClient. The archived
[03:59 crash stack](evidence/20261018T095947-319448/stderr.txt) shows render-thread
ClientBuffer release during view destruction after earlier display teardown. The
04:03 functional workflows passed after destruction-order changes, but its added
teardown workflow retained a probe connection. Current source explicitly drains
the view before display destruction, then retires connections through Qt's public
API while protocol owners are still alive. The 04:06 revision placed retirement
too late and hung; it was interrupted and retained as incomplete/failed evidence.
The 04:12 run passed all five workflows with the corrected order, including
connected-client shutdown and abrupt probe exit. This is an original
lifecycle wrapper, not an upstream Qt patch or a general crash-recovery claim.

The diagnostic review consulted [Qt's resource-management documentation](https://doc.qt.io/qt-6/qquickwindow.html#resource-management)
and [public destroyClient contract](https://doc.qt.io/qt-6/qwaylandcompositor.html#destroyClient).
The readable mutable development compositor source shows destroyClient entering
wl_client_destroy; installed 6.12.0 library disassembly confirmed that call path.
This corroborates the API used, not the exact cause of the hang or an authenticated
6.12 source checkout. No installed library was modified.

## Native application catalog and shared appearance

Original project code uses installed GLib/GIO 2.90.1 GKeyFile for desktop metadata
and Qt 6.12 public QProcess/QStylePlugin APIs. No third-party implementation was
copied into the parser or plugin. GLib retains its LGPL terms; project licensing
and Qt Wayland Compositor GPL/commercial requirements remain unresolved before
redistribution. The generic application icon and checkbox/default-button drawings
are original; installed desktop-entry artwork is not copied into the project.

Primary references read for this phase:

- [freedesktop Exec field grammar](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html): field substitution, quoting and explicit unsupported cases. The project implements a subset, not a complete desktop activation framework.
- [GLib DesktopAppInfo](https://docs.gtk.org/gio-unix/class.DesktopAppInfo.html) and [AppLaunchContext environment](https://docs.gtk.org/gio/method.AppLaunchContext.setenv.html): served development documentation identified 2.91; installed library is 2.90.1.
- [GLib 2.90.1 desktop activation source](https://raw.githubusercontent.com/GNOME/glib/2.90.1/gio/gdesktopappinfo.c), g_desktop_app_info_launch_uris_internal: D-Bus activation and spawning take different paths. Merely passing spawn flags is not proof of an explicit private-display Exec launch. The project instead constructs argv itself and refuses D-Bus-only entries.
- [Qt decoration mode](https://doc.qt.io/qt-6/qwaylandxdgtoplevel.html#decorationMode-prop) and [QtBase v6.12.0 client xdg-shell source](https://raw.githubusercontent.com/qt/qtbase/v6.12.0/src/plugins/platforms/wayland/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp), Toplevel constructor/requestWindowFlags: ordinary windows create a decoration object/request SSD; FramelessWindowHint suppresses it. Tag-labelled source was read, not signature-authenticated or established as the exact installed binary source.

Read-only disassembly of the installed compositor's decorationMode getter
corroborated its client-side fallback when no decoration object exists. Installed
public headers define client/server modes as 1/2. This supports the synthetic
frameless test design; it does not substitute for its pending native run or real
GTK/Electron qualification. Runtime implementation still uses public APIs only.
Public Qt button-layout/style interfaces supply provisional native control policy;
Qt MacLayout is not evidence of original Mac OS 9.2.2 geometry.

## Drop transport diagnostic review — 2026-10-18

Read the tag-labelled upstream Qt 6.12.0
[data-device handling](https://raw.githubusercontent.com/qt/qtbase/v6.12.0/src/plugins/platforms/wayland/qwaylanddatadevice.cpp),
[data-offer retrieval](https://raw.githubusercontent.com/qt/qtbase/v6.12.0/src/plugins/platforms/wayland/qwaylanddataoffer.cpp)
and [Quick surface input handling](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandquickitem.cpp).
These were read as upstream source references, not authenticated as the exact
installed distribution build. No upstream code was copied into the project.
The installed Qt 6.12 QWindowSystemInterface is used only in tests to reproduce
receiver dispatch. Production diagnostics use public libwayland 1.26
`wl_display_add_protocol_logger`/`wl_protocol_logger_destroy`, declared in
`/usr/include/wayland-server-core.h`. The logger is enabled only by the native-test
environment and omits protocol arguments. This is instrumentation of a still
unresolved native transfer failure, not evidence that it is corrected.

## Owned URL transport fix — 2026-10-18

The upstream Qt 6.12.0
[data source](https://raw.githubusercontent.com/qt/qtbase/v6.12.0/src/plugins/platforms/wayland/qwaylanddatasource.cpp),
constructor and data_source_send, advertises portal transfer for local URLs.
The previously cited data-offer retrieveData_sys prefers that format for
text/uri-list. This explains why a private bus without the portal can yield
empty URL data despite a working token transfer. The 10:24 trace alone does
not identify the requested MIME types; the 10:30 trace confirms an advertised
portal category, a read of the explicit owned URL format and successful decoding.

The original project workaround adds an explicit URI-list MIME format for owned
Finder drags, keeps standard URLs advertised, and decodes one bounded public
QMimeData snapshot. It uses no private production API, changes no Qt library and
does not activate or install a portal. The new offscreen regression uses a local
deferred MIME provider to model empty standard URL data; it is not a test of
Qt's internal portal implementation. Whitelisted MIME category labels were
added to the test-only protocol trace; raw arguments/payloads remain excluded.


## Exact Finder window identity — 2026-10-18

The tag-labelled [QtBase 6.12.0 Wayland window source](https://raw.githubusercontent.com/qt/qtbase/v6.12.0/src/plugins/platforms/wayland/qwaylandwindow.cpp),
QWaylandWindow::winId, returns a reinterpret_cast of wlSurface. Read-only nm and
objdump inspection of installed `/usr/lib/libQt6WaylandClient.so.6.12.0`
corroborated winId forwarding to wlSurface. This is not signature authentication
of the distribution source. The production adapter uses public Qt/libwayland
headers, but relies on that version-specific WId representation; it accepts only
Qt 6.12.0 on Wayland and otherwise returns zero. `wayland-client` is an existing
installed link dependency, not a package installed for the project. The native
10:58 gate verifies correct resolution of two same-titled windows and a child
folder on this machine. No title-based fallback is provided.

The visible product name is now Platinum Reality, per psn_tch. Stable internal
executable, style and IPC names retain `frankenstein`; this separate directory
has not been moved or merged with the protected KDE project. No new Apple assets
were acquired, copied or bundled. Marquee rendering is original QML geometry;
Finder enables Qt's existing public QListView selection-rectangle property.


## Native application appearance — 2026-10-18

All new GTK and Firefox CSS is original project source; no installed classic theme
or browser theme was copied. Read GTK 3/4's official CssProvider and CSS property
references, Mozilla's command-line/profile reference and its unsupported-custom-
CSS notice, linked in [app appearance](application-appearance.md). Installed GTK
3.24.52 and 4.24.1 parse the actual stylesheet without errors. Qt controls use
public QStyle APIs; the installed Charcoal face remains a personal dependency.

The installed Firefox metadata names version 158.0 and build 20261013210526, but
it is not an observed runtime version or complete CSS selector qualification.
An attempted read of the optimized browser omni.ja with Python zipfile failed;
no browser CSS was extracted, copied or authenticated. The Firefox adapter is
conservative, original browser-only CSS that needs live visual observation.
No Firefox execution, browser-profile import or security-preference weakening
was performed by the agent. The first GTK native attempt and its fixture/storage
failures remain preserved with their pre-run hashes; they are not a passed gate.

GIO's documented backend environment override is used to select the installed
keyfile backend for private child processes. A local two-process write/read
checks persistence under profile/config and absence of dconf runtime entries.
The source/style inputs now include CSS/C files, and native manifests include
both GTK probe binaries. The normal and instrumented probes are built from the
same source; native sanitizer execution remains unperformed.


The requested default `~/wallpaper/another1.png` is read in place with bounded PNG
header validation and used by a QML Image preserving proportions/cropping to fill.
It is not bundled. The 11:34 preview uses a disposable profile/workspace and exits
zero; its rendered capture was inspected. A chooser is not yet implemented.

The 11:35 native gate executed a disposable installed Firefox via the normal
launch adapter and passed square frame controls/process exit. Post-run launcher,
executable and metadata hashes are labelled separately as after-run observations;
they are not a full dependency manifest. Its image and the supplied user photo
show an alignment defect despite that functional pass. The subsequent regression
adds root/child surface bounds checks and a scoped native-decoration override.
No browser argv is logged; the runtime peer/profile matcher reads only bounded
process metadata for the private-display client. User history/credentials are
not read, imported or logged. GTK CSD shadows are removed with original CSS.
