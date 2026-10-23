# Touchscreen input

Qualified user-local release `0.1.0-touchscreen-20261021T124926Z` is selected
for the next user-chosen login (2026-10-21). No running session was restarted.
Read [status](status.md) before assuming the live session contains it.

The desktop and supported native Wayland applications receive touchscreen taps
and touch gestures. An application's own toolkit decides how to handle a swipe:
a browser can pan a page; a text editor can select text. Universal touch scrolling
in all Linux/X11 applications is not claimed. The existing classic inactive-window
activation policy also applies; the first tap can activate the window.

## Reproduced Firefox failure and correction

The old raw-input test passed coordinates through one wl_touch binding. It did
not establish that a real browser could click a link or scroll a page. A copied
browser fixture without preceding mouse hover reproduced a Firefox158 link-tap
failure while Chromium passed. A mouse-focus-only adjustment did not fix Firefox
and was reverted before the actual correction.

The owned Firefox protocol trace showed two wl_seat.get_touch requests, with only
one touch object receiving down/frame/up/frame. The disposable page received no
DOM touch or click. Qt6.12's
[QWaylandTouch implementation](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/compositor_api/qwaylandtouch.cpp)
selects one resource using resourceMap().value(client). The corrected path uses
all resources for the same client and seat, retaining mapped surface coordinates,
contact IDs, frame boundaries and matching serials. Cancel reaches all bindings,
including when Quick revokes the window's touch grab. This does not emit extra
mouse clicks or forward input to another client.

Private Qt touch-resource access is confined to client_surface_item.cpp alongside
its existing pointer-resource access. The workaround is guarded for Qt6.12; other
Qt versions use the upstream path and require an independent audit. The exact
primary-source URLs and hashes are retained with the development evidence.

## Evidence and limits

Development evidence: `build/artifacts/rocky-touchscreen-20261021/`. Copied before,
rejected focus-only trial and traced before/after runs are retained. The same
traced Firefox fixture passed after the delivery correction, with actual DOM
pointer/touch/click at the link, popup field focus/typing and positive page scroll.

The permanent focused suite passed seven cases (including init/cleanup): ordinary
mouse and touch browser rows for Firefox/Chromium, and raw touch/scroll coverage.
The raw client binds touch twice: both get one correctly positioned contact,
motion and lift; hiding the item cancels both; releasing a binding preserves the
remaining path. Full normal/sanitized release qualification passed56 native cases
and26 local suites per build,12 two-monitor cases per build,16 installed,25 session,
10 Extend and13 fake-wallet checks per build. Extra owned2.3x/270-degree venues
passed7 cases each. All218 frozen sources and27 released payload hashes verified.
See [permanent evidence](evidence/touchscreen-update-20261021/summary.json).

Two earlier extra sanitized runs exceeded the unchanged Firefox process-exit5s
deadline (touch5450ms, mouse5650ms), after input assertions passed. Both are
preserved. Later diagnostic controls with a15s observation measured clean exits
under1s before and after the touch fix; the final unchanged7-case qualification
passed. That does not explain the earlier slow exits or establish a shutdown fix.
No release deadline was relaxed. Leak detection was off; external apps/libraries
and the Rust bridge were not instrumented.

Browser tests inject the desktop window's Quick touch events. They do not emulate
the physical panel or the upstream Wayland input driver. Read-only inspection of
psn_tch's session found the NVTK touchscreen enabled on the rotated eDP-1 display;
a nonexclusive contact counter observed hardware events, and they confirmed Apple
menu taps. No coordinates or keystrokes were retained, no exclusive grab was used,
and no live input, calibration, hardware setting or session was changed.

After a qualified update and a user-chosen login, the physical check is tapping
Firefox links/fields and swiping a long page on the built-in panel. Rotation,
fractional scaling, multiple fingers, general X11 touch support and broad hardware
behavior remain qualification limits. Production Mirror remains disabled.
