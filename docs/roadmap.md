# Completion roadmap

Version 0.1.0 is a development prototype, not completion of the requested desktop.

This document describes source capabilities and remaining work, not the release
selected on a particular computer. Release manifests identify each candidate's
actual checks; the dated [implementation status](status.md) is a development log.
The last completed candidate gate before the wake-recovery work ran 37 component
suites, 87 full native cases and 12 two-monitor cases in normal and ASan/UBSan
builds, with separate installed/session/wallet/login checks. Those results do not
qualify later edits, physical suspend or uninstrumented external software.

Lily Hewitt's current priority is a native daily driver that replaces KDE. The next
steps are concrete service/application gaps; historical certification remains a
separate workstream.

| Area | Current state | Next gate |
| --- | --- | --- |
| Sleep and recovery | Lock coverage, disposable PAM/fprintd authentication and abandoned-lock recovery have software evidence; fresh-client wake recovery and a locked-session recovery key are in development | Resolve the reported frozen desktop after wake; actual lid/dock/authentication feedback. See [lock-screen scope](lock-screen.md). Replacing a lock client cannot repair a frozen outer compositor/DRM path |
| Graphics fallback | Capability-based buffers verified in owned GL and plain-virtio guests, with real Wayland/X11 input and pixels; see [rendering evidence](software-rendering.md) | Broader physical GPU/driver trials; VM rendering is not suspend or scanout qualification |
| Bluetooth | Selected panel, Control Strip and Applications entry; existing pairings, connection actions and default pairing agent; qualified against fake BlueZ | User's mice/headsets/controller reconnection and pairing trial |
| Saved sign-ins | Selected after13 fake-wallet checks per build and full native/session qualification | Real wallet/VS Code/Chromium sign-in trial |
| Audio routing | Sound and alert sounds selected after full release gate; offscreen panel and installed backend tested with owned virtual PipeWire | Direct native panel/virtual-server workflow, real WirePlumber and headset/microphone trial |
| Displays | Explicit Use all displays activates connected screens; remembered main-display choice follows matching monitor identities across extended logins, with failed-save rollback | Physical dock/EDID/lid and mixed-monitor performance feedback; arbitrary configuration with timed confirmation and production Mirror remain unfinished. See [display topology](display-topology-session.md) for source tests versus historical checkpoints |
| Game/application lifetime | Selected shared-X11 lifetime fix; readable supplied Steam-title fallback, native windowless apps retained | Hello Kitty Island Adventure launch/exit trial; actual games remain unqualified |
| Touchscreen | Selected multi-binding delivery fix; real Firefox/Chromium mouse and touch rows, raw cancellation/binding-release checks, owned rotated2.3x venues pass | Physical Firefox links/typing/swipe trial after chosen next login; calibration/general X11 touch and unexplained earlier exit deadlines remain open |
| Clipboard and Command keys | Bounded asynchronous writers and per-offer snapshots qualified; primary selection, command-line clipboard interfaces and Command-key routing have owned native coverage | Rich-format/application breadth; Qt reads offered formats without a byte/time budget, so bounded writers do not isolate hostile sources |
| Application breadth and session services | Browsers, X11, input capture, tray, notifications, keep-awake and lock have bounded software gates | Ordinary game/application/OAuth trials; physical authentication/suspend and remaining portal/service integration |

No selected release is replaced by an unqualified source build. Public 1.0 readiness
depends on daily use and recovery, hardware/application feedback, fresh installation
and rollback, and review of the final source/licence/export. A staged user-local
candidate is not a public release; publication still requires an explicit go.

The older milestone table below retains its original counts and scope.

| Stage | Deliverable | Current state / exit gate |
| --- | --- | --- |
| 1 | Policy, real files and owned applications | Implemented initial slice; nine local suites pass 102 checks |
| 2 | Native architecture qualification | Six combined workflows passed at 1.5x, including modal menu/chrome, connected-client shutdown and abrupt probe exit; narrow prototype gate met, broader application/protocol/recovery qualification remains open |
| 3 | Desktop semantics | Entries/placement/focus and new create/rename/duplicate/Trash, internal drops and guarded dialogs passed the four-workflow gate. Full operation/undo/reference identity, columns/sort/selection matrix remain open |
| 4 | Application breadth | Browser, launch planner, private Open With and local Qt style passed the sixth native workflow. Qualified Qt/GTK/Electron matrix, optional GMenu/GAction and isolated Xwayland/XWM remain open |
| 5 | System services | First Control Strip (Finder, Applications, Appearance), local typography/icon support and persisted animation/strip preferences implemented; native gate passed at 05:30. The eight-workflow folder-drop/context-menu gate passed; the marquee/group-drop extension also passed the 11:09 complete run. Full Control Panels, audio/display modules, notifications/tray, portals, clipboard/DnD and accessibility remain open |
| 6 | Session robustness | Private Arch preview built and qualified in nested mode. Sway-backed supervisor passes eight backend/lock-readiness/logout/crash checks; seven user-local deployment/routing checks and installed-payload smoke checks pass. User explicitly selected an experimental per-user default before physical qualification; fingerprint gate and KDE recovery remain. Physical seat/VT/output, password unlock, idle/suspend and broader recovery remain untested. No pacman install/removal performed |
| 7 | Historical qualification | Pending: authenticated target evidence, metrics/control states, menus/zoom/file/dialog/startup workflows |
| 8 | Release | Pending: license/dependency policy, reproducible package, compatibility list, measured performance/security/accessibility and recovery |

Precise typography/classic controls, content-aware zoom, desktop object behavior,
keyboard/menu tracking, dialogs, system settings and restoration need substantial
work. The sixteen Scholar questions retain unresolved aspects. Linux permissions
and preemptive execution are deliberate adaptations. Original binary compatibility
remains excluded.

Effort remains uncertain before broad native qualification. A useful experimental environment
is plausible in months of sustained engineering; broad, robust and historically
qualified desktop behavior is a multi-year maintenance commitment for a small
team. These are planning judgments, not measured estimates. Desktop ownership
does not make application-rendered controls or absent menu APIs controllable.

Reconsider Qt if correct buffer/focus/popup/lifecycle handling requires substantial
internal patches. River/wlroots remains an alternative, not a dependency to install
automatically. Keep the pure policy tests and replace the adapter if evidence
warrants it; do not redefine fidelity around a toolkit limitation.
