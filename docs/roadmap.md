# Completion roadmap

Version 0.1.0 is a development prototype, not completion of the requested desktop.

The selected release is `0.1.0-touchscreen-20261021T124926Z`: 26 local suites,
56 full native checks and 12 two-monitor checks per build (normal and ASan/UBSan),
plus 16 install, 25 session and 10 extended-session checks. See the
[current status and limits](status.md) and [handoff](../HANDOFF.md).

psn_tch's current priority is a native daily driver that replaces KDE. The next
steps are concrete service/application gaps; historical certification remains a
separate workstream.

| Area | Current state | Next gate |
| --- | --- | --- |
| Bluetooth | Selected panel, Control Strip and Applications entry; existing pairings, connection actions and default pairing agent; qualified against fake BlueZ | User's mice/headsets/controller reconnection and pairing trial |
| Saved sign-ins | Selected after13 fake-wallet checks per build and full native/session qualification | Real wallet/VS Code/Chromium sign-in trial |
| Audio routing | Sound and alert sounds selected after full release gate; offscreen panel and installed backend tested with owned virtual PipeWire | Direct native panel/virtual-server workflow, real WirePlumber and headset/microphone trial |
| Displays | Extend opt-in and fullscreen fix selected; owned two-monitor checks pass | Physical monitor/hotplug/rollback coverage; production Mirror stays disabled |
| Game/application lifetime | Selected shared-X11 lifetime fix; readable supplied Steam-title fallback, native windowless apps retained | Hello Kitty Island Adventure launch/exit trial; actual games remain unqualified |
| Touchscreen | Selected multi-binding delivery fix; real Firefox/Chromium mouse and touch rows, raw cancellation/binding-release checks, owned rotated2.3x venues pass | Physical Firefox links/typing/swipe trial after chosen next login; calibration/general X11 touch and unexplained earlier exit deadlines remain open |
| Clipboard and Command keys | Batch-1 source integration under independent normal/sanitizer qualification; held-key focus and deferred-copy ordering defects reproduced and corrected | Complete source gate, then installed/session/wallet qualification before a next-login release; later attention/primary-selection batches remain separate |
| Application breadth and session services | Browsers, X11, input capture, tray, notifications, keep-awake and lock have bounded software gates | Ordinary game/application/OAuth trials; physical authentication/suspend and remaining portal/service integration |

The wallet gate completed in Rocky's owned host venue; the other executor's
socket restrictions did not apply here. Its verified source checkpoint was saved
before Sound integration. No selected release is replaced by an unqualified source build.

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
