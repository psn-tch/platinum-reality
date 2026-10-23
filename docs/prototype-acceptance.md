# Prototype acceptance and evidence

2026-10-18 · experimental execution authorized; production deployment not performed

## Executed local checks

Eleven suites cover **130 checks**: policy 15, files 10, owned GUI 30, menu model 7,
shell 24, desktop entries 15, launcher safety 4, applications 21, style plugin 2
and installed GTK 3/4 CSS parsers 2.
Initialization/cleanup callbacks are not counted.

| Scope | Evidence | Does not establish |
| --- | --- | --- |
| Policy | Group activation/order, hide/collapse/move/restore, context/lifetime, invalid inputs, modal blocking/cycles | Native client sequencing or historical correctness |
| Files | Copy/conflict preservation, symlinks, duplicate/move/rename, Trash/restore, escape rejection, cancellation, enumeration errors and self-protection | Full metadata, storage durability or race-proof confinement |
| Owned Widgets | Double-click, spatial reuse, view/size persistence, real New Folder, edit/save, close Cancel/Discard and Meta-S | Native DnD, clipboard or broad app compatibility |
| Menu model | Labels/state, duplicate IDs, depth/item/text bounds and generations | Every provider, lazy update and fallback |
| Shell | Pointer menus and New Folder/Rename dispatch, render, preview launch rejection, menu replacement, naming Cancel/accept, nested modal tokens, exit guards and internal drag/Escape | Every native menu/popup interaction or historical alert geometry |
| Desktop entries | Selection/placement/persistence, watcher/rename/atomic-save identity, actual create/rename/duplicate/Trash, folder copy/move and failed-drop restoration, partial batch reporting and concurrent-job refusal | Alias semantics, external application drops, full undo/durability or exact historical selection rules |
| Launcher | Same-user test-bus policy, expected-file cleanup, no traversal of unknown directories/symlinks, refusal outside owned runtime | OS security confinement or full portal/session support |
| Applications | Field-code/quoting errors, literal filenames in a real owned child, URL/singular rules, bounds, XDG visibility/precedence, stale-entry refusal and persisted choices | General installed-application functionality or terminal/D-Bus-only app activation |
| Style plugin | An ordinary Widgets executable loads the local plugin, paints controls, applies the palette and uses action-right button order | Complete historical controls, ABI portability or GTK/Electron styling |

The latest additions check fixed/single-axis/modal size limits, disabled/blank
menu-bar clicks preserving selection, modal context retiring menu snapshots and
owned alert button semantics without host-theme icons. Normal and instrumented
main-executable preview-exit smoke checks from the preceding revision are recorded
separately from the current 120 checks.
Additional GUI checks cover Finder document intents, inert desktop entries,
in-session Save As/cancel and preservation of rejected UTF-8/binary input. Browser
checks use actual search, accept/cancel and queued Open With dialogs.
New animation/appearance checks cover intermediate heights, reversal without an
endpoint jump, full content size, instant mode, strip drag/folding and saved
settings. They do not exercise native client buffers or authenticate OS 9 timing.

C++ checks pass with ASan/UBSan and leak detection disabled because LeakSanitizer
cannot operate in this executor. Python checks are not instrumented by those
sanitizers. Native-only paths need separate sanitizer execution.

## Native results and current gate

The [10:58 native run](evidence/20261018T165807-489894/native-tests.txt) passes all
eight workflows in 14.225 seconds at 1.5x DPR. Desktop-to-exact-folder-window and
child-folder transfers join the seven established workflows. Native/cleanup exits
are zero, stderr is empty and all 87 pre-run hashes matched before marquee edits.
QtTest's ten passes include initialization and cleanup. This is not broad Linux
app compatibility, native sanitizer qualification or historical certification.

The [current local ledger](evidence/local-20261018T170751/manifest.json) records
120 checks in nine suites, including actual desktop/Finder marquee gestures,
selection cancellation/addition and an intact two-file desktop-folder transfer.
All 116 C++ cases also pass ASan/UBSan with leak detection disabled. The native
eighth workflow has been extended to select a group and drop it into an open
folder; the [11:09 complete run](evidence/20261018T170917-495823/native-tests.txt)
passes the extension and all eight workflows in 13.997 seconds. All 87 pre-run
hashes match; test and cleanup exit zero, with empty stderr. The actual rectangle
image was visually inspected. Compilation alone is not native evidence.
The executor cannot bind Unix sockets; `scripts/play.sh` runs the gate in a
normal graphical terminal and opens the interactive desktop after success.

The [00:35 run](evidence/20261018T063521-230970/native-tests.txt), source/binary
manifest and actual image establish a bounded real native workflow at 1.5x DPR:
mapping, frame/content alignment, first-click consumption, far-edge input, visible
exported-menu execution, stale dispatch rejection, hiding/shading, close veto and
client disconnect cleanup. It is not just a broker invocation or screenshot test.

The expanded Finder/editor scenario subsequently failed on native Command-key
handling after its folder reuse/size-persistence assertions. Those failures are
retained in the [status record](status.md). A Qt Quick menu refresh API error was
fixed with takeItem/takeMenu and an offscreen regression. Native key injection
now uses Qt's window-system event path, and a Qt 6.12-specific surface adapter
prevents modifier repair from dropping Meta. The [00:52 run](evidence/20261018T065249-240872/native-tests.txt)
passed both workflows. Exact saved bytes, a real modal Save Changes dialog,
parent blocking, Cancel preserving the prior file, a subsequent save and clean
application closure were asserted. This remains a machine/version-specific result.

The subsequent [01:08 run](evidence/20261018T070816-249724/native-tests.txt)
passed the new desktop-file workflow, including real application launches and
keyboard isolation from background documents. It failed the background folder
resize deadline in the older workflow. That run remains recorded as failed. The queued
server flush correction delivered that resize in 50 ms in the next run; no timeout
was increased. That [01:12 run](evidence/20261018T071234-252228/native-tests.txt)
then failed the menu click and post-Cancel save while the desktop workflow passed.
Menu readiness/no-op refresh handling and explicit FocusScopes then corrected
those transitions. The [01:57 run](evidence/20261018T075720-266995/native-tests.txt)
passed the complete combined gate: all three workflows, including actual editor
Quick/Wayland keyboard targets after Cancel, exact subsequent saved bytes and
desktop-file input isolation. Background resize took 50 ms; test and cleanup both
returned 0. The pre-run manifest's 51 source/binary hashes matched at archival.
The QtTest total of five includes two lifecycle callbacks, not five workflows.

The [02:20 run](evidence/20261018T082038-278578/native-tests.txt) retained those
three passes but failed the newly added fourth workflow at New Folder. Closing
the shell menu invalidated the delegate's scope before dispatch completed. The
correction dispatches from the surviving root scope, and the local naming test
now clicks the real File-menu entries rather than calling the dialog helper.
A passing combined native gate is still required for this source revision.
The [02:22 rerun](evidence/20261018T082254-279894/native-tests.txt) confirmed the
menu correction but stopped at folder acceptance while the three older workflows
passed. Current diagnostics check actual field focus, entered text and acceptance
separately. The same native modifier injection also passes the offscreen shell's
cancel/reopen/create flow; that is not evidence the live failure is fixed.
The [02:25 diagnostics](evidence/20261018T082547-282346/native-tests.txt) found
that the test typed a lowercase name without physical Shift but expected an
uppercase filename. The test helper now supplies complete native Shift chords
for its controlled US-letter/space fixtures. Exact case-sensitive expectations
are retained. Offscreen input success is still separate from live qualification.

That fourth workflow must verify create/cancel/rename, a real folder drop,
nonempty-folder duplicate, Trash/Put Away, conflict preservation and shell-dialog
input isolation from a background editor. All files are disposable fixtures.
The [02:33 run](evidence/20261018T083318-285536/native-tests.txt) passed all four
workflows in 6.327 seconds at 1.5x DPR. Test and cleanup returned 0 with empty
stderr; all 54 source/binary hashes matched at archival. Six QtTest passes include
four workflows and two lifecycle callbacks. Naming, conflict and save-dialog
captures were inspected. This closes this prototype gate, not broad compatibility
or original-system fidelity; later changes require new evidence.

The [03:59 run](evidence/20261018T095947-319448/native-tests.txt) exposed early
menu-bar hit-testing and a render-thread buffer-release crash during failure
cleanup. The [04:03 run](evidence/20261018T100304-321023/native-tests.txt) passed
the four functional workflows with the new dialog/menu assertions intact. The
fifth connected-client teardown workflow failed because the probe remained
connected. This is an overall failed run, with no crash and empty stderr. The
next gate additionally requires explicit connection retirement after renderer
destruction and abrupt-exit cleanup for a disposable probe. Normal application
closure must continue to honor Save/Cancel.
The [04:06 run](evidence/20261018T100646-323175/native-tests.txt) hung during
connection retirement and was interrupted after the first three functional
passes. The final desktop-launch case was not reached. Current ownership orders
retirement between renderer destruction and protocol destruction. The runner now
records a default 30-second per-function QtTest watchdog; timeouts remain failures.
The [04:12 run](evidence/20261018T101222-325857/native-tests.txt) then passed all
five workflows in 7.204 seconds, including connected-client retirement and abrupt
probe exit. Test and cleanup returned 0 with empty stderr, and all 56 hashes
matched before further source edits. QtTest's seven passes include five workflows
and init/cleanup. The current save-alert and post-exit images were inspected.
This establishes the bounded Qt 6.12/SHM/1.5x prototype gate, not universal client,
GPU, failure/recovery or historical behavior.

The runner uses an activation-free same-user private bus. This avoids unrelated
portal/systemd helpers during the test. Cleanup is nonrecursive and separately
reported. It does not test real portals or imply a general-purpose desktop bus.
The agent's executor still returns EPERM for Unix binding; psn_tch's normal terminal
supplies the live venue. No permission rejection is involved.

## Required next evidence

1. Qualify live menu checks/submenus, tracking, updates/provider loss and fallback
   without hidden inaccessible local menus. No universal export claim is allowed.
2. Exercise GTK CSD, non-exporting Qt, Electron and X11 separately.
3. Test native DnD, clipboard, Unicode/IME, accessibility and application launch
   activation. Folder size persistence is not full desktop/session restoration.
4. Check logout veto, failure/recovery, disconnect races, multiple outputs, scaling
   and performance. No host shutdown/reboot is part of this nested experiment.
5. Qualify history independently under the historical register. No original OS
   reference environment has been operated during implementation.

Do not install a default/login desktop while hidden input, save veto, CSD damage,
menu ownership or recovery remain unsafe or unqualified.

The modern-interaction additions check right-click selection/group preservation,
actual context actions and Escape, icon/list destination jobs, URL validation,
desktop placement/Trash/folder/collision handling and the receiver's real QWindow
drop events. They deliberately distinguish local event-path evidence from the
separately recorded seventh native workflow, which passed at 10:41 and 10:58.
The marquee/group-drop extension passed the 11:09 full native run.


## App styling qualification

The [current local ledger](evidence/local-20261018T172615/manifest.json) records
129 checks and the same 125 C/C++ checks under ASan/UBSan, leak detection off.
GTK parser success and prepared Firefox files are not native or browser rendering
passes. The first ninth-workflow run failed on the keyboard fixture's unsupported
comma, with all eight established workflows passing. The corrected native
workflow and profile-local GSettings storage await a complete live result. The
[app styling contract](application-appearance.md) preserves those distinctions.


The corrected [11:26 gate](evidence/20261018T172632-507871/native-tests.txt) passes
all nine workflows, including GTK 3/4 controls. Test and cleanup are zero; GTK
a11y-bus warnings remain. Both GTK captures were inspected. The current
[130-check ledger](evidence/local-20261018T173419/manifest.json) adds owned-browser
profile matching and a successful actual wallpaper preview. Its new tenth
native workflow opens installed Firefox in a disposable profile and checks
classic frame controls and clean process exit; this extension awaits execution.


The browser alignment correction has [current local evidence](evidence/local-20261018T174313/manifest.json):
130 checks, all eleven suites passing normally and under ASan/UBSan (Python
uninstrumented, leak detection disabled). Its native geometry test is compiled,
not executed by those local suites. The owned Firefox launch alone receives
`MOZ_GTK_TITLEBAR_DECORATION=none`; other browser profiles and other applications
do not receive this override. GTK decoration-node shadows/margins are cleared
by the existing profile-local CSS. This requests no native browser decoration
inside the classic frame. Current Wayland behavior must be established by the
real main/child-surface bounds assertions and capture, not by older X11 reports
about the same Firefox environment variable. The installed binary contains the
variable; attempts to retrieve the current large upstream implementation were
unsuccessful. The earlier control-only pass remains insufficient for alignment.
