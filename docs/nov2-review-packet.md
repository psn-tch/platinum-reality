# November 2 review packet

Branch `rocky/sammy-nov2-tasklist`, based on canonical update31 `4e3922d`.
All changes are in the separate installation-review checkout. Canonical native
sources and the selected/running release were not changed. This packet contains
qualified source checks and explicitly unqualified runtime candidates.

## Work-list status

| Item | Delivered | Actual validation / remaining work |
| --- | --- | --- |
| 1. Installed updates 30/31 | Monitors driver recognizes the single Control Panels window, sidebar and selected heading; rejects legacy/modal footers. Immediate-apply/Keep semantics documented. | 8 source/fixture checks pass. No new-image frame or installed-target run. Update31 is staged; source freeze remains until Lily Hewitt selects it. |
| 2. Real-keyboard audit | Native tests and named-key support in test-only driver; no desktop implementation fixes. | Native test binary and driver compile. Runtime blocked before tests by unavailable DRM and denied local socket binding. Enabled removable-drive eject remains a missing fixture; the new E rows test its disabled guard only. |
| 3. Firefox first-popup flake | Asserted warm-up before resize; both original Save Image As assertions retained. Separate measurement harness requires at least 200 runs, fresh scene/profile/browser per run, no skips counted as passes. | Compiles; harness help/syntax pass. **Zero measured browser runs here; no de-flake claim.** The warm-up must itself map and dismiss; a first-popup hang still fails. |
| 4. Chromium scrollbars | Standalone candidate CSS plus exact external-reference pixel checker (vertical/horizontal, 1×/2×, zero mismatch required). Not installed into any browser. | CSS is **draft/unverified**, not a fidelity pass. Chromium exits before rendering with a sandbox socket EPERM. Reference RGB crop was read and measured; screenshot stays private. |
| 5. 9.2.2 gap report | [Source-gap inventory and measured existing-reference pixels](macos9-gap-report.md), all requested subjects covered. | Fresh snapshot/QMP observations and captures are still pending because Unix socket binding is denied. No reference VM started. |
| 6. Keyboard-only installer | Complete widget walkthrough with real license/erase dialogs, Tab/arrows/Space/Return, all account fields/choices, back navigation, Return cancellation and deliberate keyboard erase. | **21/21 installer QtTest cases pass offscreen** with disposable fake engine. No actual disk/install/immutable-image keyboard proof; no KDE-theme claim for this run. |
| 7. NIC absent / installed R4 | Existing historical evidence preserved. | No new VM run. No NIC-absent install or detached installed-target upgrade/rollback boot acceptance claimed. |
| 8. Routine public export | `check-head.py` pins HEAD, invokes exporter, refuses privacy/export/inventory failures; private receipts retained. | Canonical `4e3922d`: 504 files +38 links, no findings/diff. 3 subprocess gate checks +11 exporter checks pass. No publication. |
| 9. Physical-readiness gaps | [Updated acceptance matrix and remaining storage/recovery/trust gates](../distro/docs/physical-install-readiness.md). | Documentation complete for this packet; physical-install guard remains on. Lily Hewitt authorization, backup restore, recovery and physical qualification are still separate requirements. |

## Keyboard inventory

`realKeyboardDesktopShortcuts` drives Meta/Ctrl I, O, C, A, Shift-N, D,
Backspace and the disabled E guard, plus F2 and Escape during selection/drag.
New-folder/rename fields also exercise Command-A/C/V and Command-period without
writing the cancelled name. `realKeyboardHardwareShortcuts` drives Volume
Up/Down/Mute and Brightness Up/Down with a stateful fake helper, never hardware.
`realKeyboardApplicationAndShellChords` drives forward/reverse application
switching, Hide, Quit (Meta/Alt), Control Panels Command-W, Applications Alt-D
and search-field Command-A/C/V. The shifted-command test now has desktop and
application-focus rows, clears the clipboard before the screenshot, checks its
actual dimensions, exercises the screenshot-tool aliases and Command-?. The
existing SDL capture rows now release through a real Ctrl-Alt-Escape chord.

The US virtual keymap tests layout-aware events, not all national keyboard
layouts or physical firmware. Synthetic QTest setup remains for pointer actions
and fixture state; audited keys go through the owned host Sway keyboard driver.
None of these new runtime assertions has been exercised in this executor.

## Continuation commands

Use the canonical shared heavy-work lease in a capable venue. Build this
packet's normal and sanitizer trees and run the focused keyboard functions
through the standard `with-headless-display.py` / `qualify-native.sh` venue.
Keep normal and sanitizer results separate. Do not use the running user's seat.

```bash
python3 -I -B tests/stress_firefox_context_menu.py \
  --build BUILD-DIRECTORY --lease CANONICAL-LEASE-FILE \
  --evidence NEW-PRIVATE-STRESS-DIRECTORY --runs 200

# Caller holds the same lease for Chromium:
python3 -I -B tests/chromium_scrollbars_check.py \
  --reference PRIVATE-REFERENCE-PNG --evidence NEW-PRIVATE-PIXEL-DIRECTORY

python3 -I -B distro/public-export/check-head.py --source SOURCE-CHECKOUT \
  --privacy-pattern-file PRIVATE-PATTERN-FILE
```

The stress runner retains every result and fails for any skip/failure or fewer
than 200 completed iterations; a failed display venue stops measurement. The
pixel checker refuses an unrecognized reference crop and reports mismatched
pixels rather than accepting an approximate match. Review its first actual
render before deciding whether the candidate CSS is suitable to integrate.

## Private evidence and original failures

Evidence lives under ignored `build/sammy-nov2/` in the review checkout.
`keyboard-first/backend.txt` records GLES2 failing without a DRM FD;
`keyboard-software-first/backend.txt` records the private Wayland socket refusal.
A separate owned AF_UNIX bind returned PermissionError/EPERM. Chromium's first
attempt records a Crashpad setsockopt EPERM in `chromium-first/chromium.log`.
No test reached Firefox or the reference VM. Do not count these venue failures
as skipped/passed product tests.

Installer attempts1/2 failed because offscreen has no window manager to
reactivate the parent after a modal. Attempt3 was interrupted after an unbounded
modal-input wait; attempt4 reached Finished but could not focus Restart in that
venue. Final attempt5 passes the entire suite. The fixture now supplies parent
activation **only on offscreen**, waits for the modal's actual focused widget
and sends its keys there. It never calls setFocus/setCurrentRow/setText or uses
setConfirmationAnswers in the keyboard tour. Real Wayland/X11 runs must restore
activation themselves. Original failed logs are retained; only final5 is green.

No host packages, root commands, services, login shell/PTY, browser profile,
session selection, physical disk, publication or push was performed. No VM
images or prior evidence were deleted. Source builds used the shared lease;
the lease is released after each operation.
