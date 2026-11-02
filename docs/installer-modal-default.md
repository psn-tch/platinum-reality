# Installer modal Return and destructive defaults

## Observed defect

The owned-VM graphical occupied-disk run `install-20261031T145729Z` used
native source `6505ef833072e03083bae642565877050bdc294e`, ISO SHA-256
`2fce0069b4b5f8a4acf7b20c05ee88414d14b1097d2df2eab44dbf410ef6014f`,
and engine SHA-256
`02dd983a2fa0ba3f47ec22f803c83619b0f367a95d93c1456b5d2f8b4979e1b2`.
The controller recognized the real Erase Disk question and existing synthetic
`/dev/vda1` / `vfat` / `VM_DATA` metadata, then sent **only QMP Return**.
It did not click Erase. The full 16-GiB destination hash changed:

- Before: `b5b156ec075207bcf7b94e16f01b7f1ad6322dc67beb2fc2028a58c87c1a3160`
- After: `789a76b79535431e4af1a3d5bf3f8b7731342d9d939c9d0d8d6196c1f9a218ed`

Independent visual review of `gui-11-return-changed-disk.ppm` showed
**Installing / Erasing the confirmed VM destination**. This establishes an
actual destructive-default defect, rather than only a screenshot discrepancy
or filesystem housekeeping. The controller stopped its own synthetic VM;
no physical disk, host desktop or selected release was changed.

## Mechanism and change

`ask()` set `QMessageBox::setDefaultButton(Cancel)`, but its custom role buttons
kept Qt's dialog auto-default behavior. A focused auto-default button handles
Return/Enter even when `defaultButton()` still names the other choice.
Qt documents this behavior in the [QPushButton autoDefault property](https://doc.qt.io/qt-6/qpushbutton.html#autoDefault-prop).
The distinction between the stored default and actual key activation matters:
asserting the pointer alone did not test the destructive outcome.

The fix disables auto-default on both choices, retains the deliberate default,
sets Escape to the rejecting choice, and explicitly focuses the deliberate
default before showing the question. Return/Enter then use that default;
Space and a direct click continue to choose the focused/clicked button.
The license question defaults to Agree; the erase question defaults to Cancel.
This changes neither the engine's destination checks nor the explicit final
whole-disk confirmation required to install.

## Narrow qualification, 2026-10-31

Owned copied builder, Qt 6.12.0, offscreen widget backend:

| Stage | Result |
| --- | --- |
| Test-only source `5a88ebd`, original `ask()` | Three expected result failures: erase Return and keypad Enter with Erase focused; license Return with Disagree focused |
| Fixed source `2d7241e`, full installer widget suite | 20 passed, zero failed |
| Same fixed source, ASan/UBSan build, full installer widget suite | 20 passed, zero failed; no sanitizer diagnostic |

The nine real-modal rows cover untouched initial focus, deliberate alternate
focus, Return, keypad Enter, Space and Escape. They call the actual
`ask()` / `QMessageBox::exec()` and inject keys through QtTest without preset
confirmation answers. Their engine is a trusted fake recording calls and
never modifying a disk. Every row forbids an install call.

The old implementation's untouched initial-focus cases **passed offscreen**.
That backend therefore did not reproduce the initial-focus discrepancy seen
on the real Wayland ISO. The alternate-focus failures demonstrate the key
handling mechanism, but cannot replace the real graphical VM control.

Private qualification receipt:
`build/installer-default-qualification-20261031/run-20261031T151842Z/`.
Source archive SHA-256:
`109b60db4b65eaabe669160a38a1244f05fa2dbb3ed12f43d87c3cd229e95d85`.
Patched `installer_window.cpp` SHA-256:
`6cbb0e90eeba4b7a9736210daebf337990aeb0a91464d5fe5fdd89d2ed53a85d`.

Both builds used `-O0 -g1`; the sanitizer build added
`-fsanitize=address,undefined -fno-omit-frame-pointer`,
`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`.
External Qt libraries were not instrumented. Offscreen size-hint warnings
and fake-engine process-lifetime warnings are retained in the raw logs.
The earlier `151519Z` attempt failed in test setup before the modal and is
not counted as reproduction or qualification.

## Remaining acceptance requirements

This is not a full native/session/release qualification. After integration,
rebuild a separately pinned immutable ISO and re-review its dialog recognition
fixture. On that image require all of:

1. Actual occupied-disk warning, showing the real synthetic partition metadata.
2. Return at the erase question, with no Erase click: unchanged full-disk hash
   and return to the summary page.
3. Reopen the question and deliberately click Erase: successful install,
   configured-state readback, encrypted graphical unlock, actual greeter
   authentication and desktop with installer media detached.

Do not skip the Return control to obtain a green run. Do not reuse a
source-bound screenshot hash for a new native source or changed geometry.
The QEMU/live-media-only guard remains enabled. Physical installation is
still a separate unqualified phase. Finder and other dialogs need their
own real key-path checks; this installer result is not evidence for them.

## November 1: the KDE helper also replaces the actual dialog

The update-28 image (native/distribution9d36a31, ISOc1fb6c58) failed the real
Return-only control in `install-20261101T050229Z`: the full target hash changed
and the retained frame showed “Erasing the confirmed VM destination.” Its
successful generic offscreen suite did not qualify that execution environment.

In owned diagnostic051337Z, the exact image's plasma-integration6.8.2-1 plugin
created a second visible QMessageBox. The original retained Cancel as default;
the helper's new buttons had auto-default enabled and Erase focused/default.
Return accepted. The same disk-free probe cancelled under a generic theme and
under KDE with `DontUseNativeDialog`. Original installer tests failed both
selected erase rows with the actual KDE plugin; the widget-only fix passed all
20 installer cases. Follow-up052342Z also passed Finder Trash and Editor Save
under that plugin. A generic-theme negative control failed the new loaded-theme
guard rather than silently qualifying fallback. Earlier requested-“kde” probes
without the installed plugin are excluded from KDE evidence.

The native fix keeps explicit-default questions on their own styled widget.
Package checks now require plasma-integration and run dedicated KDE cases;
the tests inspect their own loaded library maps. The Editor test also bounds
its nested dialog, because its original failing callback otherwise stranded it.
This source evidence does not qualify a rebuilt image: the actual QMP Return,
unchanged entire target, deliberate erase and detached encrypted login remain
required on that image. No physical installation or release selection follows
from these VM tests.

### Private rebuilt-image control passed

The separately pinned private source image996311d (ISO SHA256
cced40442d643c694b9056e3fd1e0bf03cce49af8177efe4ba1119c86753c1b9)
passed actual graphical run `install-20261101T054443Z`. Return was the only
action at the occupied-disk question: it dismissed to the visible summary and
left the entire16GiB destination hash unchanged:
`e4fc16a87ddf29b029d75e15cc732499b6fb150735f13d502ce7760142b162a0`.
The question was then reopened and Erase deliberately clicked. Installation,
UK English/London/UK settings readback, encrypted graphical unlock, real SDDM
password authentication and the installed desktop all passed with both CDs and
the NIC detached for the installed boot. Completion, unlock, login, desktop,
Cancel-default and returned-summary frames were independently viewed; hashes
are recorded in the run's visual-review receipts.

That image passed42 ordinary package suites, including actual-loaded KDE
regressions for Installer, Finder Trash and Editor Save. It includes the
widget-only source fix, regression guards and test/host-controller follow-ups;
it is not Sammy's canonical release and selects no installed session. Scope:
one virtio-vga output, non-GL, OVMF and disposable VM targets. Other display
fixtures, mirroring, physical installation and production release selection
require their own evidence. The older c1fb image remains failed, and the older
isolated audio cleanup failure remains unexplained; passing new coverage does
not establish a production fix for that separate failure.
