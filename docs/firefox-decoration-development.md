# Firefox decoration development

Development evidence only (2026-10-21). The selected touchscreen release does
**not** contain this adapter or the Firefox launch-setting change.

## Problem and mechanism

Some Firefox windows show a compositor Platinum title bar and a second GTK
caption. A normal-settings browser can also retain an extra close button inside
its tab strip. Passing click/typing tests alone did not detect these visual defects.

GTK3's Wayland server-decoration preference uses the older KDE
`org_kde_kwin_server_decoration_manager` protocol. The existing Qt compositor
advertises `zxdg_decoration_manager_v1`; that does not establish the GTK3
preference. The protocols are distinct. Their use together on the same surface
is explicitly undefined by the old protocol, so a production adapter needs a
per-surface conflict policy as well as resource lifetime management.

An isolated plain-C libwayland adapter advertises Server decoration, validates
surfaces/duplicate claims/mode ranges and echoes requested modes. Its global
advertisement is **not yet connected to general GTK window-frame policy**.
Firefox already has a compositor-owned frame, which makes it useful for this
limited mechanism experiment. Shipping the adapter alone is not acceptable.

Firefox's `MOZ_GTK_TITLEBAR_DECORATION=none` setting was then tested in copied
fixtures and a separately compiled manager object. In conjunction with real
server-decoration advertisement, the normal disposable Firefox profile omitted
its embedded extra close button. The browser profile was not rewritten to get
that result. Existing production launches still use `client`.

## Recorded experiments

All runs use an owned headless Sway venue, disposable browser profiles and the
canonical shared GUI lock. None touched the running desktop. Native totals
include initialization/cleanup. These are modified-harness experiments, not
qualification of the unchanged canonical source or a selected release.

| Isolated change | Native run | Result | Visual observation |
| --- | --- | --- | --- |
| Legacy adapter, existing CLIENT launch | 20261021T165052-1568833 | 4/0/0,10.819 s | Popup's second GTK caption gone; mouse/touch popup focus, typing and page swipe pass |
| Legacy adapter, existing CLIENT, owned/normal profile frame workflows | 20261021T165207-1572560 | 4/0/0,10.384 s | Platinum controls work; normal profile still has embedded browser close button |
| Legacy adapter, NONE in browser fixture | 20261021T165230-1573516 | 4/0/0,10.975 s | Popup has one caption; mouse/touch and scrolling pass |
| Legacy adapter, NONE in isolated manager, owned/normal profile frame workflows | 20261021T165451-1574866 | 4/0/0,10.564 s | Normal profile loses embedded extra close button; typing, activation geometry, shade, zoom and close pass |

All four completed with native runtime cleanup0. The screenshot of the frame
workflow is the final normal-settings row; it is not a screenshot of the user's
real browser profile. Screenshots were visually reviewed. Later focused ASan/UBSan trials are recorded below; these early runs were
normal builds.

The original GTK appearance fixture explicitly calls
`gtk_window_set_decorated(FALSE)`. It cannot establish correct default GTK
caption negotiation. A separate copied default-decorated GTK3 control therefore
requests a normal window and checks that acknowledged Server decoration actually
produces a compositor frame. That control reproduced the limitation: `20261021T165958-1591558`,2 passed/1
failed,3.289 s, cleanup0. GTK requested Server (2); input/widgets passed, then the
explicit compositor-frame assertion failed. The reviewed screenshot shows no
outer title bar/close box. The first30 s lock wait expired before a harness
started; it was a queue event, not an additional product failure.

## Required production design and acceptance

- One decoration policy per live surface. Preserve modern xdg-decoration, reject
  or explicitly reconcile conflicting old/new claims and document the rule. Do
  not silently change a functional application headerbar into an empty frame.
- Legacy None means no decoration; Client preserves application controls; Server
  produces real Platinum chrome and shade/resize/close policy. Observe negotiated
  changes before and after initial mapping, not just the first request.
- Keep protocol state in a dedicated component with bounded identity/lifetime
  ownership. Clear claims on release, surface destruction, disconnect and display
  shutdown; do not retain dead raw Qt surface pointers.
- Connect negotiated legacy state to ManagedWindow and policy transitions. The
  current Firefox forced-frame path does not solve general GTK decoration.
- Cover GTK3 default captions, functional custom headerbars and undecorated
  windows, modern Qt clients, duplicate/invalid/conflicting claims and teardown.
  The default-caption control must pass before a release.
- Test ordinary/private-setting Firefox launches, explicit user title-bar
  preferences, popups, fullscreen, picture-in-picture/undecorated intent, mouse
  and touch, rotation/scaling and settings preservation. Keep URL/tab/content
  controls and do not apply a blanket CSS rule hiding GTK headerbars.
- Then complete normal/sanitized local/native and installed/session gates on one
  frozen source batch. Preserve any failures. No live-session replacement.

A second isolated property-driven policy prototype was tested against
that failing GTK control. Its combined trial passed 9 cases in 28.530 s
(`20261021T170247-1592895`), including the formerly failing default GTK3 frame
check in private and normal settings, Firefox frame workflows and mouse/touch
popup workflows. The included original native-policy test uses a different scene
and is not evidence of adapter coexistence. A separate NativeScene Qt/X11 run
passed 4 cases in 6.188 s (`20261021T170426-1598672`); both cleanups passed.
The default GTK screenshot now visibly has Platinum chrome.

The first custom-headerbar control failed before finding its expected mapped
window: a 30 s watchdog aborted `20261021T170705-1603858`,1 passed/1 failed.
Cleanup correctly retained unexpected scratch entries (status1); after checking
that no owned client remained and the lock was free, only the exact scratch
authority/lock files were unlinked and empty directories removed. The original
failure/cleanup receipts remain unchanged. A diagnostic copied fixture keeps
the 10 s mapping limit, avoids QTRY retry doubling and reports the owned window
snapshot/title so the mapping/fixture cause can be isolated. That diagnostic established the cause: assigning the custom header title also
changed GtkWindow title. The Client (1) window had mapped correctly under the new
title. Correcting only the fixture header title, with an identical harness ELF,
passed 4 cases in 3.917 s (`20261021T171242-1615245`), cleanup0. Actual field typing,
buttons/check and header action pass in private/normal disposable settings; the
reviewed screenshot retains functional header controls with no extra outer frame.
This is a fixture correction, not a production mapping fix.

The policy prototype uses a surface property and temporarily reuses the
existing forced-frame flag to avoid changing the ABI of prebuilt root libraries;
that shortcut is experimental and must not be copied into production.

Focused instrumented policy/Firefox/default-GTK/Qt/X11 checks passed 10 cases
in 33.685 s (`20261021T171754-1619082`); custom-header checks passed 4 cases
in 5.174 s (`20261021T171931-1625081`). Both cleanups passed, with no ASan/UBSan
report. Leak detection was off; external GTK/browser processes, system libraries
and the Rust bridge were not instrumented. This is not a complete release gate.

The prototype still lacks complete dual-protocol enforcement, explicit dedicated
production state and malformed-request/lifetime stress qualification. It is not
ready for production use. [Permanent local evidence](evidence/firefox-decoration-development-20261021/summary.json)
retains failures, diagnostic correction and passing receipts.

## Primary sources and provenance

Exact retrieved source hashes and copied fixture/build commands are retained
locally in `build/artifacts/rocky-firefox-decoration-20261021/`. This developer
source note contains links, not upstream source copies or personal screenshots.

- [Firefox158 nsWindow.cpp](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_158_0_RELEASE/widget/gtk/nsWindow.cpp): GetSystemGtkWindowDecoration and SetCustomTitlebar.
- [Firefox158 nsLookAndFeel.cpp](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_158_0_RELEASE/widget/gtk/nsLookAndFeel.cpp): CSD availability/default draw-in-titlebar.
- [GTK3 Wayland window](https://github.com/GNOME/gtk/blob/gtk-3-24/gdk/wayland/gdkwindow-wayland.c),
  [display](https://github.com/GNOME/gtk/blob/gtk-3-24/gdk/wayland/gdkdisplay-wayland.c) and
  [GtkWindow](https://github.com/GNOME/gtk/blob/gtk-3-24/gtk/gtkwindow.c): decoration preference, default caption and older protocol. These branch URLs are mutable; local hashes pin the inspected bytes.
- [KDE server-decoration.xml](https://github.com/KDE/plasma-wayland-protocols/blob/master/src/protocols/server-decoration.xml): Martin Gräßlin, LGPL-2.1-or-later; retain attribution/license/source if integrated. Local SHA256 is `c5f857734bb190dc9adfb65fb05550a6245f12af1a2be5eefd05be199805a68c`.

The first helper setup stopped before harness compilation because it assumed
`c++`, while the recorded build uses `g++`; that partial directory/receipt is
retained. Later helpers redirect both compiler and linker dependency outputs and
verify recorded root ELF/object/library/Ninja metadata hashes after their builds.
An earlier touch-exit diagnostic did not redirect the linker depfile and may have
rewritten that metadata; it did not change the frozen sources or selected payload.
