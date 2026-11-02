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

Earlier qualified release: `0.1.0-native-update-20261019T071331Z`. The durable
[normal/sanitized ledger](evidence/native-update-20261019/summary.json) records
all five saving cases and the separate Firefox typing/frame case passing in each
full run. Qt linux-dmabuf-v1 is now selected on native Wayland/OpenGL when
available; the old EGL-bind initialization failure is avoided. Earlier Firefox
stalls are still an unresolved limitation, not a proven graphics-backend defect.
No private account, production profile or live download was used for these tests.

## GTK modal chooser ownership (2026-10-26)

An upload chooser behind a blocked Firefox parent is a separate, reproduced
activation defect. GTK3 sets modality using `gtk_shell1` / `gtk_surface1`, while
Qt's compositor exposes `xdg-dialog` modality. The latter alone does not receive
GTK3's request. A private unmodified-runtime negative control recorded an
explicit chooser parent with `modal=false`; activating Firefox selected that
blocked parent instead of the chooser. This is evidence of the policy mismatch,
not proof that every browser stall has the same cause.

The desktop now advertises version 1 of GTK's shell protocol, with capabilities
zero. Independently transcribed wire declarations implement set/unset modality;
the manager combines it with xdg-dialog modality and updates parent/activation
policy both on initial map and after changes. Leaving modal state recalculates
server-decoration shade eligibility. GTK startup IDs, menu metadata and timestamp-
only presentation requests grant no focus permission; global GTK menus and
xdg_foreign are not implemented by this adapter. GTK system-bell requests use the
existing alert-sound service.

The new owned Firefox case cancels twice, reactivating the application while each
chooser is open, then chooses a disposable local text file and checks the bytes
reported by the page. Another owned GTK case toggles modality after mapping,
checks modal chrome and restored controls, then exits while modal. Existing real
Firefox/Chromium Save Image As cases remain in the gate. The late-modality probe
uses the production keyfile GSettings backend inside its scene's private config
home; cleanup is still strict and does not delete unexpected runtime contents.

Primary references: [GTK3 protocol declarations](https://raw.githubusercontent.com/GNOME/gtk/gtk-3-24/gdk/wayland/protocol/gtk-shell.xml),
Mozilla's [GTK file picker](https://raw.githubusercontent.com/mozilla/gecko-dev/master/widget/gtk/nsFilePicker.cpp)
and [GTK window modality](https://raw.githubusercontent.com/mozilla/gecko-dev/master/widget/gtk/nsWindow.cpp).
These branch URLs identify the consulted source locations; local negative-control
and pre-run source hashes identify this project's tested implementation. They are
not immutable upstream version pins. The previously captured libxul futex stall
and missing-xdg_foreign warnings remain separate unresolved observations.
