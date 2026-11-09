# Desktop icon selection measurements

Rocky, 2026-11-08. Measured in an authorized disposable reflink clone with an
explicit overlay, snapshot mode, no networking and a headless display. QEMU
exited normally; clone and overlay were verified removed. Captures remain
private in ignored `build/fidelity/rocky-20261108/`; no reference artwork is
committed. This used reference installation does not establish factory layout.

A verified blank desktop click at (500,400) cleared the saved boot-disk selection.
The pointer was then moved clear after each single icon click: boot disk near
(590,53), then Sherlock near (460,41). Accepted captures are
`desktop-icon-clear-valid.png`, `desktop-icon-selected-valid.png` and
`desktop-sherlock-selected-valid.png`. Text labels and pointer are excluded from
the following icon regions. Region ends are exclusive.

| Icon region | Unchanged pixels | Changed pixels | Transform matching every changed pixel |
| --- | --- | --- | --- |
| Boot disk, x575..607/y36..59 | 433 | 303 | Maximum RGB channels halved; other channels quartered |
| Sherlock, x445..478/y27..59 | 584 | 472 | Every RGB channel halved |

Arithmetic rounds down. Boot-disk `#ccccff` becomes `#33337f`. Sherlock's
`#ccffff` becomes `#667f7f`, `#99cccc` becomes `#4c6666`, and `#cc3300` becomes
`#661900`. Only 256 of Sherlock's changed pixels match the maximum-channel rule;
all 472 match all-channel halving. Private numerical evidence is
`desktop-icon-selection-measurements.json`.

This disproves a universal maximum-half/other-quarter rule for 32px icon
selection. It does not by itself establish why the drawings differ. Resource
format, palette or separately supplied artwork are hypotheses, not measured
facts. Do not infer a rule from PNG bit depth: the current importer chooses
il32/is32, then indexed, then monochrome input, but emits RGBA PNG for all of
them without retaining the source distinction.

`src/desktop/DesktopIcon.qml` currently applies a translucent blue-black overlay
to imported selected icons and separate colors to geometric fallbacks. Neither
has been qualified against these measurements. No desktop paint/input change
is made from an unsupported universal rule. The first Finder icon transform was qualified to its Graphing Calculator
capture. Later ordinary-folder/Sherlock evidence corrected the generic default
to RGB halving (see `finder.md`); palette exceptions still need source-family
provenance before a broad fidelity claim. Its real selection,
manual placement and file operations remain intact.

Desktop label backgrounds, inactive application transitions, aliases, colored
labels and factory spacing require separate measurements. These captures do not
justify changing those behaviors.

## Read-only source-family audit

A disposable reflink copy was converted to a temporary raw image and read with
our existing importer readers. No VM ran for this audit. Only resource types,
IDs, lengths and bundle mappings were retained in private
`icon-provenance.json`; the clone, raw image and extracted forks were removed.
The original image was used only as the reflink-copy source.

The actual `System Folder/System Resources` folder family(-3999) contains both
`il32`/`l8mk` and `is32`/`s8mk`, as well as indexed and monochrome alternatives.
The generic hard-disk family(-3995) also contains true-color data. Its presence
does not establish that this is the captured boot volume's chosen icon.
Graphing Calculator's BNDL maps local application icon0 to resource128; its large
icon resources are indexed/monochrome, with no `il32` or `icns` family found.

Sherlock's application BNDL also maps local icon0 to128 and has indexed128
artwork, while numerous other `icns` IDs contain32/48px true-color data. Its
Desktop/Apple-menu files are aliases without their own icon families. The audit
does not associate a particular true-color resource or rendering path with the
captured desktop alias. Therefore simply finding `il32` somewhere in a fork, or
finding `icl8`, is insufficient to select a rule. Resource association, alias
composition and the drawing path remain open questions. No automatic importer
classification is introduced from this evidence.
