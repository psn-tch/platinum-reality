# Graphical installer recognition fixtures

The owned QMP driver targets the reviewed native `6505ef8` installer at
1280×800, one unit-scale virtio-vga output, and the executable live Desktop
shortcut. It is not a general GUI automation or accessibility implementation.
Unknown geometry or missing evidence stops before the next action. No native
frontend test-answer API is used in these VM proofs.

## Focus-ring update and retained failed runs

Update26's control geometry and default-button rings exposed limitations in
the earlier whole-window OCR fixture. Retained failures on corrected image
`2fce0069…` were before erasure:

- `142421Z`: sparse OCR omitted the real Agree button; a body-word click did
  not consent. The actual license dialog remained visible.
- `142917Z`: the correct short name was entered, but the old fixed text crop
  cut off the taller field's glyphs.
- `143225Z`: Return advanced to the summary while verifying a combo selection.
- `143844Z`: OCR omitted the actual Install button; explanatory prose was clicked.
- `144437Z`: the actual occupied erase question appeared, but OCR omitted the
  ringed Erase button and read the partition path's final digit as lowercase l.

None is counted as a completed GUI installation. The failed screenshots and
unmodified receipts remain private artifacts under their original run names.

The corrected driver reads the reviewed button-text interiors, anchors account
text crops to the actual recognized label row, and selects advertised combo
entries through closed-control Home/Down/Tab without activating the default
Continue button. It still requires the selected value on the actual label row.
Replay of the actual captured frames validates the intended targets before a
fresh full VM run; replay itself sends no VM input and is not a runtime pass.

## Exact occupied path pixels

The final `1` in `/dev/vda1` is consistently misclassified by OCR in the reviewed
frame. The driver does **not** replace l with 1, force the OCR alphabet, or accept
a guessed path. Its fallback requires native6505ef8, the corrected02dd983a engine,
1280×800, and the exact RGB bytes of rectangle `(230,285,63,19)` from the manually
inspected144437Z question. Its SHA256 is
`d9f5c25aeb735536b9dc1256ad6f84ffd4ff979da68486f1440bd23a586b689e`.
Actual immutable-engine planning and the prepared kernel partition independently
agree on that path. Type, label and full removal warning must still be recognized.

The private provenance record and rejected OCR variants are retained alongside
that frame. Different source, geometry or a single changed pixel rejects this
fallback. This is an exact graphical fixture, not semantic recognition of arbitrary
text or authentication against hostile software. A layout/font/native change
requires a new independent review, not a silently relaxed predicate.

## Destructive default control

`--verify-erase-return` requires occupied GUI mode. After recognizing the real
question, it sends only Return, captures the result, and rehashes the entire
synthetic disk against its pre-question baseline. It requires an unchanged disk
and the visible summary before reopening the question and explicitly clicking
Erase and Install. A changed disk or an uncancelled question fails; it cannot
be credited from a button's appearance or a mocked confirmation answer.

This control is runtime-pending until an actual receipt records it. Its mocked
tests distinguish changed-disk failure from unchanged-disk/visible-summary success.
Only newly created invocation-owned VM targets are involved; no physical disk,
host session, installed host application, or production release is modified.
