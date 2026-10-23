# Browser saving and popup focus

psn_tch reported two failures: right-click Save As opened nothing in Firefox and
Chromium; ChatGPT's download button said it could not save the file. These are
tracked separately. A successful local fixture is not proof of the actual site's
workflow with the user's profile, account and extensions.

Claude reproduced the right-click failure on the private display. Qt-created
popup/subsurface items acquired keyboard focus on a pointer press. Qt's xdg-shell
focus handling then deactivated the parent window, and each browser dismissed
its menu before the release activated the selected command. ClientSurfaceItem
now keeps these generated items from taking focus on click; manager window
policy remains in control. Its deferred child reconciliation also preserves the
existing fractional-scale geometry corrections. A Qt 6.12 hover-enter adapter
maps item coordinates into surface coordinates like the move/button paths.

Native tests open each installed browser in a disposable home/profile, right-click
an actual image, select the browser's own Save Image As row, check focus/chooser
activation and fitting, and inspect saved PNG bytes under the typed filename.
Other fixtures verify an HTTP image download, actual Save As/cancellation,
JavaScript Blob downloads and server attachments. Chromium also exercises
showSaveFilePicker and actual saved bytes. All files stay in disposable homes;
fixtures silence audio and disable add-on onboarding without changing user profiles.

The file-picker portal is intentionally absent on the private bus. Firefox falls
back to GTK and can save successfully. Its missing-portal message alone does not
establish a download failure. Chromium's GTK chooser consistently required its
Save button in the current test; Return did not complete it. This remains open.
Initial oversized chooser fitting preserves the activation most recently sent by
the manager, including before the client acknowledges it.

A test-readiness race was found during Rocky's resumed full gate: initial fitting
could replace the one-shot Menu fixture ready title with a size title before the
test observed it. Browser fixtures now announce loaded layout with the current
width and explicitly wait for the requested 800-pixel layout. No save assertion
was removed. Firefox's typing fixture also confirms DOM focus and layout before
its single pointer click, then asserts real text entry and focus reacquisition.

Known limits: Firefox has intermittently stopped responding in the software
rendering path; Claude captured futex waits in libxul threads before a popup
request. Root cause and relevance to the live seat remain unproven. Retain failed
runs and do not call it an upstream defect or resolved based on a passing retry.
The current compositor's graphics-buffer support and broader popup keyboard/grab
behavior require further work. Qt 6.12.0 itself leaves xdg_popup.grab unimplemented;
the parent-focus workaround is not full protocol conformance or universal menu
keyboard compatibility. Test pointer activation, Escape, keyboard menus, nested
popups and application switching independently before expanding those claims.

Primary implementation references: Qt 6.12.0
[Quick-item input](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandquickitem.cpp)
and [xdg-shell focus/popup handling](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/extensions/qwaylandxdgshell.cpp).
The installed stable xdg-shell XML distinguishes activated state from keyboard
focus and specifies popup-grab keyboard behavior. No system Qt library is patched.

The typing fixture now uses a normal 32px-high form control and derives its hit
point from the DOM viewport height. The former viewport-height single-line input
produced a missing initial mouse-down/focus result and remains an inconclusive
nonstandard-control case. Reacquisition waits for DOM focus and acknowledges End
before appending, avoiding accidental word selection/replacement during rapid
activation clicks. The exact existing and appended text remain asserted. A first-
run Privacy Notice tab was also suppressed only in this disposable fixture, as
in other browser fixtures; that alone did not resolve its failure.

The first typing click now follows a cursor request matching the known test
browser's pointer-enter serial. Earlier failing traces sent enter/motion/press
in the same synthetic burst, before that toolkit acknowledgement; mouseup reached
the DOM without mousedown, and Tab remained responsive. This fixture synchronization
is not a new protocol acknowledgement requirement for production clients. The
original single pointer focus and exact text/reacquisition assertions remain.

Current qualified release: `0.1.0-native-update-20261019T071331Z`. The durable
[normal/sanitized ledger](evidence/native-update-20261019/summary.json) records
all five saving cases and the separate Firefox typing/frame case passing in each
full run. Qt linux-dmabuf-v1 is now selected on native Wayland/OpenGL when
available; the old EGL-bind initialization failure is avoided. Earlier Firefox
stalls are still an unresolved limitation, not a proven graphics-backend defect.
No private account, production profile or live download was used for these tests.
