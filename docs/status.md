# Implementation status

> **Development log.** Paths, defaults, machines and earlier project names here describe the
> developer's setup when each entry was written. Current behaviour is in the README and
> [docs/install.md](install.md).

## Clipboard safety candidate — 2026-10-22

The clipboard / Command-key batch plus its safety follow-up, integrated by Sammy after
Rocky ran out of usage. Pastes from the compositor-owned clipboard go through Rocky's bounded
writer group (8 at once, non-blocking, 30 s idle / 120 s total, SIGPIPE consumed per thread) and
read the snapshot of the clipboard their offer was made for. New native tests with an owned raw
client reproduce the old failures: a stalled reader stopped the compositor (2005 ms), early
closes killed it with SIGPIPE, and 64 stalled readers stopped it again; on the first,
unbounded fix they made 96 threads and an old offer pasted a later copy. The private session bus
keeps the installed session bus's limits, and tray and application-menu clicks expect no reply.
Before that, 128 of 600 clicks on a hung application arrived and the desktop's later calls
failed, as in the live session. Release gate: `build/artifacts/sam-clipboard-safety-gate-20261022`.
Not selected until that gate passes and psn_tch selects it. Details:
[transfer safety](clipboard-transfer-safety.md), [tray and notifications](tray-and-notifications.md).

## Clipboard integration candidate — 2026-10-21 (superseded by the safety candidate above)

`work/clipboard-20261021` integrates Sammy's reviewed clipboard / Command-key
batch, separately from his attention and primary-selection work. Independent
review reproduced and corrected stale held-key state across a focus switch and
an older deferred shell copy overwriting a newer application selection. Image
preview decoding now checks source dimensions before allocating the image.
Normal and sanitized source gates passed 71 native / 26 local / 12 two-monitor
checks each, plus installed/session qualification. **Release remains blocked:**
the newly enabled Qt retained-selection sender writes synchronously and may
freeze the compositor or deliver SIGPIPE when a paste reader stalls or closes.
Sammy's standalone safety patch is under independent review; the unfixed candidate
must not be selected. Passing ordinary clipboard workflows did not cover this path.
The selected touchscreen release below remains unchanged. See the
[integration review](clipboard-integration.md) for evidence and residual limits.

## Selected touchscreen update — 2026-10-21

`0.1.0-touchscreen-20261021T124926Z` is selected for the next user-chosen login.
All wl_touch resources belonging to the same client/seat receive mapped contact
events, matching serials and cancellation. This fixes the reproduced Firefox
link-tap failure without synthesizing additional mouse clicks. Browser tap, popup
field typing and page swipes are exercised without preceding pointer hover.
Ordinary Firefox/Chromium mouse rows remain unchanged.

Normal/ASan-UBSan:26 local suites,56 full native cases,12 two-monitor cases and13
fake-wallet checks per build;16 installed,25 session and10 Extend checks. Extra
owned rotated2.3x venues passed7 cases each. All218 frozen sources,27 payload files,
10 released ELF hashes and five protected login files verified. Runtime cleanup
passed; no live restart, physical input injection or hardware change. See
[permanent evidence](evidence/touchscreen-update-20261021/summary.json).

Two earlier extra rotated-ASan runs failed at the existing Firefox process-exit
5s deadline, after functional assertions passed (5450ms touch,5650ms mouse). They
are preserved. Diagnostic-only15s observations later measured clean exits under
1s in both pre-touch and fixed builds; the final unchanged original7-case run
passed. The earlier slow exits remain unexplained; this is not a shutdown fix.
Production browser exit limits remain5s. Leak detection remains off; external
programs/libraries/Rust are uninstrumented. Physical touchscreen/calibration,
universal X11 touch support, real authentication/wallet/audio and Mirror remain
open. [Touchscreen scope](touchscreen.md) distinguishes software and hardware.

## Earlier application-lifecycle update — 2026-10-21

`0.1.0-app-lifecycle-20261021T122114Z` is selected for the next user-chosen
login. Closed X11 games no longer retain Application-menu claims merely because
the shared bridge is alive. Other windows/peers retain their claims; windowless
native apps and Finder survive. Unknown Steam-ID entries use a bounded supplied
window title, with catalog names preferred. This adapts Linux application lifetime;
it does not certify an actual game. Tray-only X11 apps remain in the Control Strip.

Normal/ASan-UBSan:26 local suites,54 full native cases,12 two-monitor cases and13
fake-wallet checks per build;16 installed,25 session and10 Extend checks. All218
frozen sources,27 payload files,10 released ELF hashes and five protected login
files verified. Native runtime cleanup passed. No live restart or real game run.
See [permanent evidence](evidence/application-lifecycle-update-20261021/summary.json).

The superseded first gate and focused Vulkan failures are retained. Six sanitized
Wayland Vulkan rows exceeded20s before creating a window; the identical probe
measured22.647s under plain owned Sway/KDE. The new ASan-only startup budget is35s
with a60s function watchdog; normal remains20s/30s. Default sanitizer stack depth,
frame/pixel/input/resize/exit assertions remain. Leak detection stays off; external
applications/libraries/Rust bridge are not instrumented. This is a test startup
budget correction, not a Vulkan product fix. Real games/authentication/wallet/audio,
production Mirror and broad physical qualification remain open.

Touchscreen is the next priority: hardware contacts detected without grabbing
the device; psn_tch confirms Apple-menu taps but reports app taps/swiping broken.
No touchscreen fix had been installed at that checkpoint. Earlier sections retain their counts.

## Earlier Bluetooth launcher update — 2026-10-21

`0.1.0-bluetooth-launcher-20261021T112439Z` is selected for the next user-chosen
login. **Alt+D → Bluetooth → Return** opens the existing Control Strip panel.
It uses the same service, starts no extra agent, stays out of Open With and
restores the active application's keyboard input on close. A disk application
entry cannot replace this reserved internal action.

This release includes Sam's staged Sound, alert sounds, dialog keys and modeless
Get Info work. Normal/ASan-UBSan:26 local suites,52 full native cases,12 two-monitor
cases and13 fake-wallet checks per build;16 installed,25 session and10 Extend
checks. All218 frozen source inputs and27 payload files verified; five protected
login files unchanged. No running session restarted. Leak detection off; external
programs/system libraries/Rust bridge uninstrumented. Physical audio, wallet
unlock/retained sign-ins, authentication/suspend and broad hardware behavior
remain user trials. See [evidence](evidence/bluetooth-launcher-update-20261021/summary.json)
and the [handoff](../HANDOFF.md). Earlier sections retain their historical counts.

## Earlier saved-sign-ins update — 2026-10-21

The [wallet integration](saved-signins.md) is implemented in source, with physical
login-only activation, a three-service allowlist, native-display prompt routing,
owned daemon lifetime and matching kwallet6 backend arguments for recognized
applications. Explicit preferences are preserved; an unavailable requested
wallet does not automatically downgrade a recognized application to basic storage.

`0.1.0-signins-20261021T080008Z` is selected for the next user-chosen login.
Normal and ASan/UBSan qualification:23 local suites,13 fake-wallet checks,51
full native checks and12 two-monitor checks per build, plus13 install,25 session
and10 Extend checks. All26 payload and five protected-file hashes verified.
No running session was restarted or real wallet opened. Real unlock/sign-in
retention and physical Bluetooth/audio remain user trials. The preceding
Bluetooth release is retained. See the [release evidence](evidence/wallet-update-20261021/summary.json).
The earlier blocked-executor results, failed prompt runs and intermittent Firefox
popup-input focus failure are preserved; the subsequent pass is not a Firefox fix.

Sound is integrated into main development source after preserving the qualified
wallet source checkpoint. The hardened helper, device panel and two menu entries
have focused offscreen and owned virtual PipeWire evidence. Sound is not in the
selected release; native panel and installed/release qualification remain pending.
See the current [handoff](../HANDOFF.md) before building or selecting anything.

## Selected Bluetooth update — 2026-10-21

The preceding `0.1.0-bluetooth-20261021T061204Z` was selected at this checkpoint.
It passed 21 local suites per build, 51 full native checks per build and 12
two-monitor checks per build (normal and ASan/UBSan), plus 13 install, 25 session
and 10 extended-session checks. Native counts include data rows and
initialization/cleanup. All recorded inputs matched before staging, and all 25
selected payload hashes and five protected-file hashes were verified afterward.
The [release summary](evidence/bluetooth-update-20261021/summary.json) and
[deployment review](evidence/bluetooth-update-20261021/deployment-review.json)
record the update and retained predecessor. No running session was restarted.

The [Bluetooth service](bluetooth.md) lists existing BlueZ pairings, connects and
disconnects devices, shows battery status and provides Add Device and pairing
alerts. Session tests prove the installed desktop registers as the default
agent on a fake BlueZ bus, refuses impostors and waits for the user. Hardware
pairing/reconnection/audio is unverified. This release also includes the saved
Extend opt-in and its full-screen crash fix. Production Mirror remains disabled;
saved sign-ins through the KDE wallet remain the next development task.
Leak detection is off; external applications/system libraries/bridge are
uninstrumented. Earlier gate failures are preserved with the release evidence.

The sections below describe earlier checkpoints and their original counts.

## Mirror pointer/cursor development — 2026-10-21

The selected popup release below is unchanged. A development pointer foundation
passed local20×2, full51×2 and two-monitor13×2 on its archived source. The next
cursor-copy experiment passed two-monitor15×2 and ordinary pointer4×2 on its
separate final source. All counts include native initialization/cleanup where
applicable. The [development summary](evidence/mirror-development-20261021/summary.json)
and [handoff](../HANDOFF.md) preserve exact source boundaries and earlier failures.

The cursor prototype uses actual image/hotspot readback, buffer updates and hidden
game cursors with either source monitor. It is compiled only for the opt-in test
target. Non-bitmap shapes currently use an original arrow fallback. Production
Mirror, absolute/touch input from either copy, drag/drop, mode transitions and
display rollback remain unfinished. No new release was installed or selected.
The earlier Firefox first-click failure and inconclusive old/new pointer
comparison remain recorded; stronger DOM target readiness is not a claimed
production Firefox focus fix.

## Selected popup update — 2026-10-21

The selected release `0.1.0-popups-20261021T005842Z` passed 20 local suites per build,
51 native checks per build and 11 two-monitor checks per build (normal and
ASan/UBSan), plus 13 install, 20 session and 10 Extend checks. Native counts include
data rows, initialization and cleanup. The final gate is
`build/artifacts/rocky-popups-release-gate-20261021-final`; [HANDOFF.md](../HANDOFF.md)
records exact run IDs and preserved failures. The [release summary](evidence/popups-update-20261021/summary.json)
and [deployment review](evidence/popups-update-20261021/deployment-review.json)
record selection for the next user-chosen logout/login. The predecessor remains
available and no running session was restarted.
Leak detection is disabled and external apps/libraries/bridge are uninstrumented.

This batch keeps popup menus within their monitor with protocol-correct flip
offsets and exact fractional geometry. Experimental Extend adds X11 fullscreen
on the requested monitor and uses the saved KDE arrangement/primary. Extend
remains opt-in; production Mirror, physical hotplug and display rollback are
unfinished. Authentication fixtures use disposable PAM/fprintd and cannot claim
the real fingerprint reader; physical authentication remains unqualified.
The Finder fixture's activation/selection waits follow a preserved sanitizer
watchdog; final passes do not establish a production Finder bug fix.

The sections below preserve earlier release history and counts.

## Current native update — 2026-10-19

The [current ledger](evidence/native-update-20261019/summary.json) records all
13 local suites / 151 checks passing normally (8.83s) and under ASan/UBSan
(9.48s). The [normal native gate](evidence/native-update-20261019/normal/native-tests.txt)
passes all 19 functional workflows plus init/cleanup in 50.275s; the
[instrumented gate](evidence/native-update-20261019/sanitize/native-tests.txt)
passes the same cases in 57.136s. There are no failures or skips, and both private
runtimes were removed safely. All 131 normal / 132 instrumented pre-run input
hashes still matched afterward. Leak detection is disabled. External Firefox,
Chromium, Qt/GTK and other system libraries are not instrumented; external
browsers use a separately recorded normal-build style plugin during ASan runs.

This revision includes Alt+D / Alt+Q, Desktop Picture, Puzzle, screenshot crop/
save/cancel, quick controls, exact-known Ymir wrapper integration, popup focus and
fractional hover-enter fixes, and activation-preserving initial window fitting.
Puzzle arrows now use window shortcuts: a platform-window regression failed
before the fix because Shuffle swallowed keys, and passes after it. Tab/Space
on Shuffle remain available. Quick controls are tested against stateful fake
services; no real brightness/radio/volume changes were made by automatic tests.

Qt's installed linux-dmabuf-v1 compositor integration is selected for native
Wayland/OpenGL when no override exists. It avoids the previous missing
EGL_WL_bind_wayland_display initialization path. Client SHM remains supported;
this is not measured proof of browser/emulator hardware acceleration. Earlier
Firefox context-menu stalls remain an open limitation. The two current gates
pass, but neither a root cause nor a universal fix is claimed. Qt 6.12.0 popup
keyboard grabs, Chromium chooser Return, actual ChatGPT saving with psn_tch's
profile, Xwayland and wider GPU applications remain open.

The [selected user-local release](evidence/native-update-20261019/deployment-review.json)
is `0.1.0-native-update-20261019T071331Z`: 13 payload files, with all six ELF files
matching the build. Nine installed smoke checks and eight nested session checks
pass; windowed/fullscreen captures were inspected. The user launcher and login
selection now target it. The running session was not restarted; activation is at
the next user-chosen logout/login. The old keyboard release and KDE fallback
remain available. The desktop-crash test intentionally retains its inner runtime
without recursive deletion; this is recorded in the session manifest.
A separate [emulator/GPU investigation](emulators.md), after release selection,
proves one Radeon SDL OpenGL client presents correct composed pixels and logs a
successful Ymir OpenGL UI launch. Its welcome-modal placement remains unresolved;
no extra renderer/output-policy changes were installed.

The sections below preserve earlier qualification and deployment history.


## Standalone session candidate — new work

The user subsequently requested default-session deployment before physical
qualification. The preview is now installed per-user in
`~/.local/share/platinum-dreams-native/`. The post-fingerprint tty1 command in
`~/.bash_profile` invokes a five-second selector (Platinum Reality default, K for
KDE). The exact profile backup and deployment hashes are retained. Protected
projects, the fingerprint gate, KDE settings and system packages are unchanged.
Seven disposable-home installer/recovery/routing checks pass. Actual physical
startup, password unlock and suspend/resume remain untested; this is an explicitly
selected experimental default, not a certified session.
The [deployment ledger](evidence/login-default-20261018/deployment-review.json)
records source/payload/profile hashes. Nine checks pass against the actual
user-local installed payload; its installed supervisor also rendered and exited
cleanly in nested mode at scale 1.5. Existing PulseAudio/PipeWire server queries
succeeded from a private runtime; no playback claim follows from those queries.

The user selected Platinum Reality as the whole login desktop. A separate session
supervisor now uses installed Sway 1.12 for display/seat/input and swaylock for the
outer screen lock. [Eight nested backend checks](evidence/session-backend-20261018-b/manifest.json)
pass, covering startup protection, control robustness, native lock readiness,
graceful close/logout, and normal/crash/startup-failure outer cleanup. The tests
record a deliberately retained inner runtime after the compositor-kill case.
No C++/QML or qualified package content changed in this phase.

Physical DRM/VT/output, password unlock, suspend/resume and real idle monitoring
remain untested. The [session notes](standalone-session.md) describe the
installed runner, pinned release, console observation gate and recovery.
This new gate is separate from the qualified nested-desktop results below.

## Earlier qualified revision — 2026-10-18

The agent can now bind Unix sockets and run live tests directly. All **131 local
checks in 11 suites** pass (125 C++, two C GTK parser cases and four Python
checks); the 127 C/C++ checks also pass ASan/UBSan with leak detection disabled.
The [current local ledger](evidence/local-20261018T202931/manifest.json) includes
individual case output and hashes; normal/instrumented times are 4.62/5.75 s.

The [complete normal native gate](evidence/20261018T202519-591851/native-tests.txt)
passes ten workflows in 27.500 s. The later [complete native sanitizer gate](evidence/20261018T202825-595314/native-tests.txt)
passes all ten in 28.289 s, with safe cleanup and all 103 pre-run hashes checked.
External Firefox and system libraries are not instrumented by the project.
The source differs by additional read-only Finder activation/selection fields
and explicit test waits before Put Away. The preceding instrumented failure is
preserved at `20261018T202609-593390`; no sanitizer memory report occurred there.

Firefox frame/content alignment now passes at 1.5x. Owned launches use client
titlebar mode and tabs-in-titlebar, hiding duplicate HTML caption buttons. GTK
decoration shadows are absent. Public subsurface position signals are captured
from surface creation; child views retain floating point size/position after Qt's
default view/stacking work. Native captures were inspected. QML reconciliation
is guarded against synchronous reentry, eliminating the observed collapse binding
loop in the current complete runs. EGL SHM-path, GTK accessibility-bus and popup
grab warnings remain evidence limits, not suppressed successes.

A [staged installed preview](evidence/install-debug-20261018T2029/manifest.json)
passes desktop-entry validation, help, absent-display failure, windowed/full-screen
native startup/cleanup and duplicate-profile rejection. Both rendered captures
were inspected; fresh system icons use the initial full-screen output size and
saved placements remain preserved. This is a nested native desktop, not a tested
display-manager login session. The [installation plan](install.md) describes the
private Arch package and rollback. No package has been installed on the host.

The [final Arch package review](evidence/package-20261018T203329/manifest.json)
records a successful `makepkg` release build and all 131 local checks. The
[release native gate](evidence/release-20261018T203459-607588/native-tests.txt)
passes all ten workflows in 27.574 s, cleanup zero and all 103 recorded inputs
matching. Its native test runs unstripped build outputs. The actual stripped
package was extracted without installing; its [nine launcher checks](evidence/install-release-20261018T2035/manifest.json)
pass, including owner-only fresh profiles, duplicate-session rejection and
windowed/full-screen startup/cleanup. Both package captures were inspected.
Five packaged ELF files resolve their installed dependencies. The archive has
exactly 11 payload files plus three package metadata files; no services, login
sessions, autostart entries or proprietary assets are included. Two independent
source archives were byte-identical; binary package reproducibility is not claimed.

Ready artifact: `build/packages/20261018T203329/platinum-dreams-preview-0.1.0-1-x86_64.pkg.tar.zst`.
SHA-256: `073986a201d5d46c0de963d8a50320dc54e5d7d06b1026d4c6dcc2b9a6c68e09`.
The host package query confirms it is not installed. Actual pacman installation,
removal and standalone login-session use have not been exercised. Installing this
preview adds an Applications entry for a full-screen nested desktop; it does not
replace the selected desktop session. Exit applications normally before ending
the nested session or removing the package. User workspace/profile files remain
outside pacman's payload and are retained on removal.

## Earlier development record

The chronological entries below describe earlier revisions, including the former
socket restriction and browser alignment failures. They do not override the
current results above.

psn_tch subsequently reports Firefox opening successfully inside Platinum Reality.
This is a user-observed launch result, not an automated browser qualification.
Version/backend and browsing, downloads, clipboard, media and restart behavior
remain unrecorded. See [application observations](application-compatibility.md).
The [hands-on guide](try-it.md) now distinguishes direct launch of the existing
build from the automatic qualification gate. The later application-style and browser-frame work is recorded below; the
earlier user launch observation remains separate from automatic qualification.

**Native experimental desktop foundation; not a finished desktop.** The combined
native core, Finder/editor and desktop-file workflows have passed in psn_tch's graphical terminal. The agent executor
still cannot bind Unix sockets, so new live checks require that terminal.
The complete eight-workflow native gate passed at 10:58, including desktop-to-owned
folder-window move, child-folder copy, collision preservation and shared-client
shutdown. The marquee/group-transfer extension also passed the complete 11:09 native run. Broad application support and complete historical
fidelity remain unfinished.

## Verified local checks

Eleven suites pass **130 checks**: policy 15, files 10, owned Widgets 30, menu model 7,
Qt Quick shell 24, desktop entries 15, launcher safety 4, applications 21, style
plugin 2 and GTK 3/4 CSS parsers 2. Lifecycle callbacks are excluded. All 126 C/C++
checks also pass with ASan/UBSan; four Python checks are uninstrumented. LeakSanitizer cannot
run here and is explicitly disabled. These are not native sanitizer results.

Current local [test output](evidence/local-20261018T201036/local-tests.txt),
[instrumented output](evidence/local-20261018T201036/sanitizer-tests.txt) and
[after-run manifest](evidence/local-20261018T201036/manifest.json) record all eleven
suites passing in 5.04/6.15 seconds. The local test inputs and binaries are unchanged;
this revision changes native-only diagnostics and disposable GTK probe handling.
An earlier isolated offscreen preview exited zero;
its [wallpaper capture](evidence/local-20261018T173419/wallpaper-preview.png) was
visually inspected. Earlier marquee, transfer, application and settings-storage
checks remain in the suite. These results do not qualify native Firefox chrome.

The earlier [115-check ledger](evidence/local-20261018T165632/manifest.json) and
[10:58 native run](evidence/20261018T165807-489894/manifest.json) describe the prior
revision. All 87 native pre-run hashes matched before marquee edits began. Native
results must be read against their own manifests. The preceding [preview ledger](evidence/local-20261018T150219/smoke-results.json)
also records main-executable preview-exit smokes; those were not repeated for this
selection revision and do not exercise native client buffers.

## Application browser and hands-on gate

Implemented a searchable installed-application catalog, explicit argv launch
planner, profile-local MIME choices, Desktop/Finder Open With, a live-peer-checked
private Finder bridge, build-local Widgets style plugin and CSD-preserving frames.
Finder Open no longer invokes the host default opener. Its selected-icon path
also now handles partial model rows correctly. Owned Editor uses Widgets file
pickers and rejects binary/invalid/incomplete UTF-8 without retaining an original
save path. The root folder presents as Macintosh HD.

The new local checks exercise actual picker search/accept/cancel and queued
requests, stale metadata, XDG precedence/visibility, a real disposable argv/env
probe, dynamic style loading, Save As and rejected-document byte preservation.
The application/Open With/owned-alert images were inspected.

The sixth native workflow exercises the browser, project-local style
in a separately launched Qt probe, SSD versus frameless content, Finder's actual
document opening, rejection of an impersonating bus peer, Open With, and saved
application preferences. It passed with the other five workflows in psn_tch's
[04:55 run](evidence/20261018T105533-348013/native-tests.txt), 9.373 seconds,
test/cleanup 0 and empty stderr. All 73 pre-run hashes match the preceding local
manifest, not the subsequently changed source. The recorded
[venue preflight](evidence/local-20261018T150219/ipc-venue.txt) still returns 77/EPERM here.
No GTK/Electron or installed third-party application was automatically executed.

[scripts/play.sh](../scripts/play.sh) runs the complete native gate, then opens the
interactive session only after success. The [hands-on guide](try-it.md) shifts
the next user check from repeated automated clicks to active exploration. Failure
stops at the printed evidence directory. No native pass is inferred from compilation.

The [latest native run](evidence/20261018T174654-522022/native-tests.txt) failed the
Firefox bounds assertion and GTK 4 text input, with eight established workflows
passing. Its 95 recorded inputs matched before further edits; cleanup exited zero.
The GTK helper continued querying destroyed widgets after its window closed.
It now ends its loop on unrealize, unregisters its reporting object, and guards
closed-widget queries. The native fixture also follows host window resizes like
production, waits for entry focus before typing, asserts checkbox state, and emits
bounded numeric surface geometry with failure captures. These changes compile in
both builds but have not run natively. They do not establish the cause of the
window closure or resolve Firefox bounds. The agent socket preflight still returns
77/EPERM after psn_tch reported changing session permissions. No host-side relay or
security-setting edit was introduced.

## WindowShade, Control Strip and typography

The new WindowRoll clips a live full-size client instead of changing its buffer
size. It animates in both directions and can reverse mid-transition. The adapter
withholds client keyboard focus and menu dispatch during motion, then restores
focus only if the application is still eligible. Expanded popup content is not
permanently clipped. Speed, strip visibility, folded state and position persist
atomically in appearance.json. The initial Appearance panel is a shell modal,
not a complete recreation of the original Appearance control panel.

The strip has three working native modules: Finder activation/opening, Applications
and Appearance. Audio/display and other system modules remain unimplemented.
Charcoal is selected from the installed font database, with a DejaVu fallback
elsewhere. The rainbow icon is read from the installed NineIcons theme or an
explicit profile PNG. Neither font nor icon is copied into the project.

New local checks cover intermediate frames, smooth reversal, instant mode,
unchanged content dimensions, settings persistence/validation, real strip drag/
folding, dialog Cancel/accept and hiding/recovering the strip. The Save As check
now waits for the actual filename field and enabled Save button, rather than
accepting a file picker before its initial directory/model is ready.

The native source now checks live intermediate frames, blocked input in both
directions, full client size and post-animation focus restoration. Its application
workflow also clicks the Finder strip module. **These changes passed the 05:30 native run.**

## Native evidence

| Run | Outcome and scope |
| --- | --- |
| 2026-10-18 00:29 | Initial native probe passed; its image exposed client/frame scaling mismatch |
| [00:35](evidence/20261018T063521-230970/native-tests.txt) | Updated core probe passed at 1.5x DPR: matching client/frame bounds, first-click consumption, far-edge input, visible global-menu command, stale dispatch, hide/shade, close veto and disconnect cleanup |
| [00:41](evidence/20261018T064112-234568/native-tests.txt) | Core passed; Finder/editor workflow failed at save. Invalid synthetic Meta scan codes and a nonexistent Qt Quick Menu.clear call were found |
| [00:45](evidence/20261018T064516-237113/native-tests.txt) | Core passed; native save shortcut still inserted letters. Menu API and launcher cleanup defects were corrected; keyboard path required further work |
| [00:52](evidence/20261018T065249-240872/native-tests.txt) | Both workflows passed: core probe plus real Finder cross-process folder reuse, restored window size, exact Command-key saves, modal parent blocking, Cancel-preserved contents and clean application closure. Test and runtime cleanup both returned 0; stderr was empty |
| [01:08](evidence/20261018T070816-249724/native-tests.txt) | Core and new desktop-file workflows passed. Combined run failed because a background Finder resize exceeded the unchanged 5-second deadline. The new workflow launched real Finder/editor clients from desktop entries, moved an icon and verified background typing did not dirty the editor. Cleanup returned 0 |
| [01:12](evidence/20261018T071234-252228/native-tests.txt) | Background resize reached 700×390 in 50 ms; desktop-file workflow passed again. Combined run failed on the visible exported-menu click and the second save after modal Cancel. Cleanup returned 0 |
| [01:57](evidence/20261018T075720-266995/native-tests.txt) | All three native workflows passed in 3.419 seconds at 1.5x DPR. Background resize took 50 ms. Visible menu execution, exact saves, post-Cancel keyboard targets, desktop launches/input isolation and closure assertions passed. Test and cleanup returned 0; stderr was empty |
| [02:20](evidence/20261018T082038-278578/native-tests.txt) | The three established workflows passed. The new fourth desktop-operation workflow failed opening New Folder: closing the menu destroyed its delegate's QML scope before dispatch finished. Test returned 1, cleanup 0, stderr empty; all 53 pre-run hashes matched before the correction. Failed evidence is retained |
| [02:22](evidence/20261018T082254-279894/native-tests.txt) | The menu correction opened the dialog; the new workflow then failed waiting for the accepted folder name. The three established workflows passed. Test returned 1, cleanup 0, stderr empty; all 53 hashes matched. Focus/text/acceptance diagnostics and matching offscreen native-modifier coverage were added; cause remains under investigation |
| [02:25](evidence/20261018T082547-282346/native-tests.txt) | Diagnostics found lowercase `cancelled folder` where the test expected `Cancelled folder`. QTest's character injection had not pressed Shift. The three older workflows passed; test 1, cleanup 0, empty stderr, all 54 hashes matched. Test input now supplies explicit native Shift chords; exact case-sensitive expectations remain unchanged |
| [02:33](evidence/20261018T083318-285536/native-tests.txt) | All four native workflows passed in 6.327 seconds at 1.5x DPR, including desktop create/cancel/rename, real folder drop, nonempty-folder duplicate, Trash/Put Away, collision preservation and background-document isolation. Background configure took 51 ms. Test and cleanup returned 0, stderr empty, all 54 pre-run hashes matched at archival |
| [03:59](evidence/20261018T095947-319448/native-tests.txt) | First dialog/menu revision stopped at the first File-menu click, then crashed during failure cleanup (test exit 139, cleanup 0). The render-thread stack shows ClientBuffer release through a torn-down Wayland resource. All 55 hashes matched. Menu-bar layout readiness and renderer/display destruction ordering were corrected; the crash evidence is retained |
| [04:03](evidence/20261018T100304-321023/native-tests.txt) | All four functional workflows passed, including retained/disabled modal menus, rejected current/stale Save actions, fixed alert controls, Cancel focus and exact subsequent save. The new fifth teardown workflow failed because its probe connection remained open; no compositor crash or stderr. Test 1, cleanup 0, all 56 hashes matched. Explicit connection retirement after renderer destruction and abrupt-probe-exit coverage were then added |
| [04:06](evidence/20261018T100646-323175/native-tests.txt) | The first three functional workflows passed; the new retirement path hung during teardown and the final desktop-launch workflow was not reached. psn_tch interrupted it with Ctrl+C after about 99 seconds in that case. No test-exit file was written; cleanup returned 0. All 56 hashes matched at archival. Retirement now runs after the renderer but before protocol destruction; the next native gate is pending |
| [04:12](evidence/20261018T101222-325857/native-tests.txt) | All five workflows passed in 7.204 seconds at 1.5x DPR, including the four functional workflows plus connected-client display shutdown and abrupt probe exit. Explicit retirement completed; background configure took 51 ms. Test and cleanup returned 0, stderr empty, all 56 hashes matched at archival. Manifest records the 30000 ms function watchdog |
| [04:55](evidence/20261018T105533-348013/native-tests.txt) | All six workflows passed in 9.373 seconds at 1.5x DPR, adding the real application browser, dynamic Qt style, SSD/frameless geometry, Finder document bridge/peer rejection and Open With choices. Test and cleanup 0, stderr empty, all 73 hashes match the preceding local evidence ledger. This predates Charcoal, the local Apple icon, animated WindowShade and the Control Strip |
| [05:30](evidence/20261018T113057-363421/native-tests.txt) | All six workflows passed in 10.557 seconds at 1.5x DPR, including live rolling frames, retained client dimensions, guarded input, focus restoration and the Finder strip module. Native/cleanup exits 0, empty stderr, all 79 pre-run hashes match the preceding local ledger. This predates the new drag/context-menu revision |
| [09:51](evidence/20261018T155116-459034/native-tests.txt) | First folder-to-desktop transfer failed: the expected Moved.txt destination did not appear. Native context-menu opening, drag startup and wrong-token rejection passed; later transfer cases were not reached. All six established workflows passed. Test 1, cleanup 0, empty stderr; all 84 pre-run hashes matched the preceding local ledger/current inputs at archival. No transport root cause is established by this run. |
| [10:24 full](evidence/20261018T162401-470790/native-tests.txt) / [focused](evidence/20261018T162429-471241/native-tests.txt) | Both stop on empty decoded files, with the 36-byte drag token received and native delivery completed. All six established workflows pass in the full run. Both test 1, cleanup 0, empty stderr; all 84 hashes in each manifest matched the preceding local ledger and current inputs at archival. These traces isolate the failure to URL retrieval/decoding. The reported helper startup failure during teardown was an expected SIGTERM mislabelled by the error handler. |
| [10:30](evidence/20261018T163016-474466/native-tests.txt) | Move, Option-copy, Escape-cancel and collision-preservation assertions pass. The helper reads the explicit owned URL format and decodes one file. The next list-view menu click fails before the background-folder/list-source drag and desktop Duplicate are reached; all six established workflows still pass. Test 1, cleanup 0, empty stderr, all 84 hashes match the local file-list-fix ledger. Native menu readiness is corrected next; this remains a partial run, not a seven-workflow pass. |
| [10:34](evidence/20261018T163405-476656/native-tests.txt) | All new file/menu assertions reached completion, including list-source transfer into the background folder and desktop Duplicate. Closing both Finder windows then crashed in NativeManager::synchronize at surface->client()->processId while the same client's other window was being removed. Test 139, cleanup 0; stack captured in stderr. All 84 hashes matched current inputs before edits. The fix batches client-window removal before synchronization and guards live peer lookup; this is still a failed run. |
| [10:37](evidence/20261018T163756-479032/native-tests.txt) | The complete new drag/menu/close workflow passes, along with five of the six established workflows. The older policy workflow stops while opening its exported menu, before invoking the command. No crash; test 1, cleanup 0, empty stderr. All 85 hashes match the cleanup-fix local ledger and current inputs at archival. The subsequent native-only change waits for provider readiness and polishes/re-acquires the menu row/items; the next full gate is pending. |
| [10:41](evidence/20261018T164110-480975/native-tests.txt) | All seven workflows pass in 13.542 seconds at 1.5x DPR, including all new transfer/menu assertions and clean shared Finder-client closure. Test/cleanup exits 0, empty stderr, all 85 pre-run hashes match current inputs at archival. Relative to the 113-check local ledger, only the native-test source/binary changed for menu-readiness waits. Native warnings remain recorded, including the separate WindowRoll binding-loop warning. |

The 00:35 input/source/binary hashes were checked against the then-current files
before subsequent changes. Its [image](evidence/20261018T063521-230970/native-observed.png)
confirms the original 1.5x content-size defect was corrected. The native test is
one workflow per test slot; QtTest's totals also include init/cleanup.

The 00:52 pre-run manifest's 46 source/binary hashes were checked against current
files before further edits. QtTest reports four passes because its two workflow
slots also include initialization and cleanup. This qualifies the narrow Qt 6.12
Meta adaptation on this machine, not every shortcut or Qt version. Earlier failures
remain archived alongside the successful run.

The 01:08 source/binary manifest also matched before subsequent edits. Its
[desktop image](evidence/20261018T070816-249724/native-desktop-files.png) shows
the real launched applications and moved file icon. It is partial native evidence
from an overall failed run. The queued outgoing Wayland flush corrected resize
delivery in the next run, which exposed additional menu and focus timing defects.
Current source waits for menu preparation, avoids identical model rebuilds and
uses explicit window FocusScopes. The native test now checks the post-Cancel
editor's actual Quick/Wayland keyboard targets. The 01:57 run passed the complete
combined gate with those assertions intact. All 51 pre-run source/binary hashes
matched current inputs when the run was archived. QtTest's five passes consist
of three workflow slots plus initialization and cleanup.

The successful run includes actual [desktop/application](evidence/20261018T075720-266995/native-desktop-files.png)
and [save-dialog](evidence/20261018T075720-266995/native-save-dialog.png) captures.
This qualifies the recorded prototype workflows on this machine, not general
application compatibility, repeated-run stability or complete historical fidelity.

Native rendering currently qualifies the shared-memory path on this machine.
The EGL bind-extension warning remains recorded; it does not establish DMA-BUF,
zero-copy, other GPU drivers or arbitrary modern applications.

## Launcher corrections

The initial test bus auto-activated existing portal helpers and left an empty
runtime directory. That directory was removed using empty-directory operations.
Both nested launchers now use a same-user private bus with no service dirs
or host configuration includes. Runtime cleanup never recursively traverses
unknown directories, symlinks or mounts. The 00:41/00:45 runs recorded cleanup 0
and empty stderr. No host service/configuration edits were used to suppress errors.

Each new run saves source/binary hashes, test output, stderr, test status and
cleanup status under build/artifacts/native-runs. Failed runs are retained.

## Desktop entries

The shell now lists ordinary files/folders in `workspace/Desktop`, plus disk and
Trash entry points. Real pointer selection/dragging, group offsets, persisted
positions, filesystem notifications and atomic-save replacement are locally
checked. Linux birth identity supports ordinary rename continuity when available;
name fallback and hard links have narrower guarantees. This is presentation
identity, not an alias implementation or a Macintosh Desktop Database.

Finder has a persistent logical application identity even after its folder client
exits. Desktop activation has no client keyboard target and uses real shell-owned
File/Edit/View commands. The policy refuses desktop activation through an owned
Finder modal dialog. These are native design contracts awaiting historical
qualification. No additional Scholar question is closed.

## Desktop file operations — current qualification

New Folder and Rename use shell-owned naming dialogs; Duplicate and Move to
Trash operate on selected real entries. Internal pointer drops move into eligible
folders, Alt-drops copy, Trash drops remain recoverable, and Escape restores a
drag's original placement. Failed drops restore placement and never overwrite
collisions. Linux filename extensions are retained by duplicate naming to preserve
native associations; this is an adaptation, not verified 9.2.2 behavior.

One asynchronous job runs at a time. Selection changes during a job are respected;
partial batches report completed items and stop at the first failure. There is no
running-job cancellation, transaction rollback or storage-durability guarantee.
Shell modal tokens block client input and normal nested-session exit while any
dialog remains open. Dialogs and jobs are locally checked, including actual menu
clicks after the native failure exposed the missing regression path.

The fourth native workflow exercises create/cancel/rename, internal drag, duplicate,
Trash, Finder Put Away, collision reporting and an untouched background document.
The [02:33 combined run](evidence/20261018T083318-285536/native-tests.txt) passed
that gate with all assertions retained. Its QtTest total of six comprises four
workflows plus init/cleanup. Actual [naming](evidence/20261018T083318-285536/native-desktop-naming.png)
and [collision](evidence/20261018T083318-285536/native-desktop-conflict.png) captures
were inspected. This qualifies these workflows on the recorded version/machine;
it is not historical certification or a repeated-run stability claim. Finder's private read-only
DirectoryState query reports asynchronous listing/job readiness; it does not
perform file actions or replace the test's pointer and keyboard input.

## Still incomplete

Broad application/category support and robust menu fallback; Xwayland; GTK menus;
complete desktop operations/persistence; stable folder/document references; historically qualified
zoom, selection/rename/menu tracking; settings/portals; clipboard/DnD/IME and
accessibility; notifications/tray; lock/logout/recovery; outputs/scaling matrices
and performance. Desktop and Finder marquee selection are implemented; inline rename, external
application file drops, complete undo/cancellation and stable references remain absent. The
internal file-operation slice has passed its narrow native gate. Finder and desktop entries
are limited to the selected workspace. Visuals and historical behavior remain
provisional. No Scholar uncertainty is closed by native implementation tests.

The [04:03 save-dialog capture](evidence/20261018T100304-321023/native-save-dialog.png)
shows a centered plain alert frame with original monochrome artwork, text-only
buttons, no close/zoom/shade/grow boxes and the editor's retained disabled menu.
Its native assertions passed. Resizable modal pickers are still allowed by client
size hints; fixed-size windows refuse server resize/zoom. These presentation rules
are native adaptations, not authenticated target control geometry or every classic
modal menu exception. Broader dialog/provider behavior remains open.

The new teardown workflow deliberately closes a nested display with a disposable
probe connected, then exercises abrupt exit of another test-owned probe. Its
acceptance requires renderer cleanup before display destruction, explicit client
retirement and no orphan probe. The 04:12 run passed after the 04:03 and 04:06 failures.
It does not kill user applications or replace normal save/cancel negotiation.
The [04:12 combined run](evidence/20261018T101222-325857/native-tests.txt) passed
this gate. The renderer finished before connections retired, while their protocol
owners were still alive. Both the deliberately disconnected probe and the abruptly
stopped probe left no managed window behind. QtTest's seven passes comprise five
workflows plus init/cleanup. The current [save alert](evidence/20261018T101222-325857/native-save-dialog.png)
and [post-exit capture](evidence/20261018T101222-325857/native-after-client-exit.png)
were inspected. This does not certify arbitrary crash recovery or every driver.
The native runner now defaults to a 30-second QtTest function watchdog, recorded
in each new manifest; a timeout is failure, never a skipped or passing result.

All source/build edits remain in the independent project or owned test fixtures.
The existing KDE Platinum Reality repositories, their management/evidence, Potato
and KDE configuration were not edited. This separate prototype now uses the
visible product name Platinum Reality; its working directory and internal IDs
remain unchanged. No root operations, package installation, VM/emulator, original Macintosh
program, deployment, Git initialization, commit or push was performed. No Apple
asset is bundled; licensing/distribution is undecided.

## Modern file interactions

The [modern conveniences contract](modern-conveniences.md) records the requested
adaptation: native file drags out of Finder folders and standard right-click
menus. Local GUI tests use actual menus and view drop events; desktop tests check
selection, command lifetimes, helper QWindow events and real asynchronous jobs.
Name collisions preserve the two conflicting versions; successful earlier batch
items remain completed and are reported. The new seventh native workflow checks
actual cross-process Wayland transfer and a background folder target. It passed the 10:41 and 10:58 full native gates. No installed third-party app was run.

## Folder-to-desktop transfer investigation

The [09:51 run](evidence/20261018T155116-459034/native-tests.txt) established a
failed first transfer but lacked helper/error diagnostics. The [10:24 focused
trace](evidence/20261018T162429-471241/native-tests.txt) subsequently records
successful drop delivery, two data requests and receipt of the 36-byte token,
followed by zero decoded files. It is an empty file-list failure, not a helper
startup timeout. The earlier helper-stop warning was also produced by our own
SIGTERM during teardown; the handler now distinguishes FailedToStart from
expected retirement and leaves unexpected-exit reporting to finished().

Qt's upstream 6.12.0 source automatically advertises portal file transfer for
local URLs and prefers that route when retrieving standard URL data. Our private
bus deliberately has no portal service. This provides a source-supported
explanation for the empty list; the earlier trace did not label MIME arguments.
The new trace labels only whitelisted MIME categories, never payloads or tokens.
It directly confirms that Qt advertises portal-files and that our corrected
receiver reads owned-file-urls and decodes a file. The installed distribution's
exact upstream build provenance has not been authenticated.

Finder now additionally advertises the same encoded URI-list bytes as
`application/x-frankenstein-local-file-urls`. Owned destinations decode that
bounded snapshot without requesting the portal-backed standard format. Standard
file URLs remain advertised for interoperability. Validation still rejects whole
invalid batches, remote/relative paths, NULs, more than 128 URLs and decoded
lists above 256 KiB; malformed owned data never falls back to another format.
The destination retains job ownership and collision/source-preservation checks.
This is an owned-app bridge, not a general portal implementation.

The new deferred-MIME receiver regression [fails before](evidence/local-20261018T162714/portal-regression-before.txt)
and [passes after](evidence/local-20261018T162714/portal-regression-after.txt) the fix.
It models an empty standard URL read and an available owned format, through Qt's
window-system drag dispatch. It does not execute Qt's portal implementation or a
Wayland pipe. Seven further cases check encoded names/deduplication and invalid
owned payloads without weakening bounds. All nine local suites pass 112 checks;
108 C++ checks also pass ASan/UBSan with leak detection disabled, while the four
Python checks are uninstrumented. Production source is unchanged by the later
native-test-only readiness correction.

The [10:30 native run](evidence/20261018T163016-474466/native-tests.txt) then passes
the real move/copy/cancel/collision assertions and all six established workflows.
It stops at the new list-view menu selection: this new test clicked an exported
menu without waiting for AboutToShow/GetLayout, unlike the established menu test.
The corrected test waits for the broker to finish preparation, re-acquires an
enabled visible delegate, polishes its layout, clicks it, and requires the menu
to close and Finder's view to change. No fixed sleep, longer timeout or removed
file assertion substitutes for that interaction. Background-folder/list-source
transfer and desktop context-menu Duplicate still need the complete native gate.

The native capture also records a WindowRoll binding-loop warning in a passing
established case. That separate warning remains unresolved.

## Shared-client shutdown correction

The [10:34 stack](evidence/20261018T163405-476656/stderr.txt) records a null client
dereference while retiring one of two windows on an exiting Finder connection.
A surface QPointer can remain non-null after its client is gone. The manager now
defers native synchronization while removing every window/application claim of
that connection, then applies focus/menu/configure effects once. Public surface
peer lookup returns zero for null or unattached clients; dead peers cannot retain
a menu provider or receive focus/configures. The renderer/display destruction
order is unchanged. The [10:37 run](evidence/20261018T163756-479032/native-tests.txt)
passes the entire new workflow and its real shared-client closure.

All nine local suites pass 113 checks (109 C++ also under ASan/UBSan with leak
detection disabled, plus four uninstrumented Python checks). The added local
case covers missing/unattached peer identity, not a real client disconnection.
The preserved native workflow is the runtime regression for that sequence.
A separate older exported-menu opening failed in the same combined run. Its
new readiness/layout correction is test-only and still needs a complete gate.

## Seven-workflow native qualification — preceding revision

The [10:41 complete run](evidence/20261018T164110-480975/native-tests.txt) passes all
seven workflows. QtTest reports nine passes because initialization and cleanup
are included. All 85 source/binary hashes were checked before subsequent work.
The local ledger differs only in native-test source and the rebuilt native binary;
production source and all local-suite inputs match. Both the successful evidence
and earlier failures remain preserved. The native-desktop-context image was
captured before the popup painted; it shows the transferred files and list view,
while successful Duplicate is established by the actual pointer/file assertion.
The shared-memory EGL warning, unimplemented popup-grab warning, unavailable
portal/theme notices and WindowRoll binding-loop warning remain visible. This
is not a clean-warning, native-sanitizer or general application qualification.


## Exact-window drops and marquee extension

The [10:58 complete run](evidence/20261018T165807-489894/native-tests.txt) passes
all eight workflows in 14.225 seconds at 1.5x DPR. QtTest reports ten passes with
init/cleanup. Native and cleanup exits are zero; stderr is empty. It verifies
correct desktop drops into one of two same-titled folder windows, child-folder
copy, source preservation on collision/cancel, chrome rejection, denial of an
untrusted bus connection and closure of both windows. Finder item counts follow
filesystem changes. The Qt 6.12.0 Wayland surface-ID representation is explicitly
version-gated, and unsupported versions fail closed; it is not a portable WId
contract. The 115-check local ledger belongs to this prior native revision.

The current eighth workflow additionally draws a real desktop selection rectangle,
checks Escape restoration, drags a selected pair into the correct open folder,
and asserts both destination byte sequences and absent sources. It is compiled
in normal and instrumented builds; the extension passed the complete 11:09 run. Local
120-check evidence covers those source-model/shell interactions and Finder's
icon/list background gesture. Holding the left button on empty space selects;
Shift/Ctrl-drag adds on the desktop, and starting on a selected item drags its
group. These are requested native convenience choices, not target certification.


The [11:09 complete native run](evidence/20261018T170917-495823/native-tests.txt)
passes all eight workflows in 13.997 seconds, including the new rectangle,
selection-cancel restoration and group drop. Both destination byte sequences
match and the two sources are absent; the other same-titled folder stays empty.
Native and cleanup exits are zero, stderr is empty, and all 87 pre-run hashes
match the current source/binaries. The [live selection capture](evidence/20261018T170917-495823/native-desktop-marquee.png)
was inspected and shows two selected files within the rectangle below the two
folder windows. The local 120-check ledger's 112 inputs also remain unchanged.
This qualifies the bounded owned-app workflow on this machine, with the existing
EGL/SHM, popup-grab and WindowRoll warnings retained; it is not historical,
third-party-application or native-sanitizer certification.


## Application appearance on launch — current slice

[App styling](application-appearance.md) now covers an expanded local Qt Widgets
plugin, original GTK 3/4 CSS and an explicit separate Firefox browser-chrome
profile. No host settings, existing browser profile or installed theme files are
changed. Firefox's browser controls remain visible and no web content CSS is
injected. Its selectors and repeat launch still require live visual qualification.

The [11:23 run](evidence/20261018T172338-505721/native-tests.txt) passes all eight
established workflows with the new source, but fails the ninth before its text
assertion: the test's typeAscii helper deliberately rejects punctuation, while
the new phrase contained a comma. This is a fixture error, not evidence of GTK
losing that character. Cleanup returns 1 because GTK created `dconf/user` in a
runtime directory. It remains retained; no recursive cleanup was introduced.
All 95 pre-run hashes matched before the correction was made.

The corrected fixture uses letters/spaces and keeps exact text, button, font,
rendering and closure assertions. Child GSettings uses the keyfile backend in
its private profile. A new local check writes a setting, reads it in a separate
process, checks its profile file and requires an empty runtime. The [129-check
ledger](evidence/local-20261018T172615/manifest.json) records normal and instrumented
full-suite passes in 4.64/5.32 seconds, with all 126 source/binary hashes current
at sealing. The Qt control capture was visually inspected. Native probes are
compiled, not counted as local runtime qualification; the corrected nine-workflow
gate still needs the graphical terminal. Existing warnings remain tracked.


The [11:26 complete native run](evidence/20261018T172632-507871/native-tests.txt)
passes all nine workflows in 14.377 seconds, including actual GTK 3/4 typing,
button clicks, computed Charcoal fonts, captures and closure. Test and cleanup
exit zero. All 95 pre-run hashes matched before the subsequent browser-frame
work. GTK accessibility-bus warnings remain in stderr; no accessibility service
or support is being certified. The two GTK captures were visually inspected.
psn_tch also reports the styled Firefox looking good and requests classic frame
controls; that manual report is separate from the automated GTK result.

## Firefox frame and requested wallpaper — current slice

The requested `~/wallpaper/another1.png` exists and was inspected. The default
background now reads that personal file, preserves proportions and crops to fill.
Missing/invalid/oversized input falls back to the solid color. The file remains
in place and is not bundled. A wallpaper chooser/persistence UI is not implemented
by this default-image change. The preview uses a disposable profile/workspace,
not the currently running hands-on profile or host KDE wallpaper settings.

For the dedicated Firefox profile, the manager adds its existing classic frame
even when the browser has not negotiated server decorations. This is a targeted
presentation adapter: the live private-display peer must be an installed Firefox
executable, and bounded process argv must select exactly the canonical owned
browser profile. No titles or app IDs are treated as authority and no argv is
logged. The dedicated browser CSS removes duplicate browser caption buttons;
the compositor supplies close, zoom and WindowShade controls. Other applications'
functional client decorations remain intact. The native tenth workflow opens
a disposable Firefox on about:blank, checks resize, real square controls and
process exit. It is compiled and awaits its live result. Local profile matching
and the default background preview pass; they are not a native browser pass.


The 11:35 native run (`20261018T173511-513512`) passed its ten functional workflows,
including Firefox frame actions and clean process exit, but its actual capture
and psn_tch's `~/o/issue1.png` reveal a browser-content offset and overflow. That run
does not establish correct frame/content alignment. The correction removes native
Firefox decoration margins only for the owned browser launch and removes GTK
decoration shadows. The Firefox native workflow now checks window geometry,
main-surface bounds and all live child-surface bounds before capture and after
zoom/restore. The correction still needs its own complete native result.


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
