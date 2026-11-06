# Unassigned Command chords in shell fields

Review branch `rocky/shell-command-text-nov3`, based on update32 `6d7b1b3`.
Canonical sources and the selected/running release are unchanged.

A printable Meta-N key event inserted `n` in all six tested desktop fields:
Applications search, Keyboard's try field, create/rename, screenshot path and
Desktop Picture's folder/path. The original key helper supplied no text and
could not expose this behavior. The component regression supplies the text a
real key carries; 18 additional native rows send N/B/G through the owned host's
US-layout virtual keyboard.

`ShellTextCommands` still claims and executes its existing edit commands.
Unassigned printable Command presses now pass through with key, modifiers,
native metadata and timestamp preserved, and insertion text removed. A guarded
forward prevents recursive filtering. Their ShortcutOverride stays unclaimed:
window shortcuts run first, and QML item handlers still receive N/W. External
application surfaces and ordinary/control/option typing retain their paths.
The first-session guide and Help Center explain the resulting behavior.

Validation in the executor, November 3 UTC:

- Before: all six component rows insert the unwanted letter and fail.
- After: six field rows plus note N/W, panel W and Command-A/C controls pass;
  9/9 QtTest cases including init/cleanup, normal and ASan/UBSan. Leak detection
  is disabled in the sanitizer venue as in the project's standard checks.
- Normal full desktop suite: 62 pass, 3 fail at required local socket listeners
  (EPERM in this executor). No suite-wide pass is claimed. An earlier run also
  lacked its screenshot artifact directory; that venue setup was corrected.
- Native test binary compiles. The new 18 real-keyboard rows are **pending** a
  socket/DRM-capable venue. No native or daily-use gate is claimed here.

Original failures, build logs and final outcomes remain in ignored
`build/nov3-followup/command-field-*`. No installation or activation is needed
to review the source. Integration/gating is reserved for the maintainer.
