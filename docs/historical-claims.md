# Historical contracts and unresolved questions

Draft · 2026-10-18 · No original reference system operated

## Platinum Scholar Historical Evidence Integration

The read-only source collection is
private research notes (private research notes).
Its Phase 1–3 evidence is historical research, not this project's implementation
architecture. No claim here marks a Platinum Reality feature DONE.

Labels preserve the collection's distinctions: **V922** is a prior researcher's
inspection of a still from a recording displaying 9.2.2, with provenance limits;
**D9/D91** is Apple documentation for 9.0/9.1; **C** is earlier Classic Apple
documentation; **I** is inference; **U** is unresolved. **Adaptation** below means
a Frankenstein design decision, not Scholar's separate product-policy label.

There is no fully authenticated target Finder Help corpus. Phase 2 partially
advanced eleven composite questions but completely resolved none of sixteen.
Phase 3 identified acquisition/authentication steps; it did not admit Help
contents. See the original bibliography (private research notes),
question disposition (private research notes)
and acquisition handoff (private research notes).

## Claims affecting the current source

| ID | Bounded claim / evidence | Project contract | Still unresolved |
| --- | --- | --- | --- |
| H01 | **C:** applications and their windows are distinct; original HIG Application Menu and Window Display Order, Scholar S06/S13 | Explicit app/window IDs and group state | Exact 9.2.2 grouping exceptions and menu ordering |
| H02 | **C:** menu ownership belongs to the active application, Scholar S04–S06 | Captured logical context is separate from shell keyboard focus | Tracking details, client deactivate effects, target exceptions |
| H03 | **V922:** title-bar-only and expanded states appear in the archived recording stills R01/R02 | Model collapsed state | Input, animation, focus, defaults and transition sequence are not proved |
| H04 | **C:** Mac OS 8 HIG printed pp. 103–104 describes movable/closeable collapsed windows and restoration at their moved location, Scholar S03 | Keep body extent; update origin on move | Target-version carry-forward, modal cases, close veto and client-initiated size changes |
| H05 | **D91:** Finder Window-menu selection uncollapses; documented modifier alternatives, Apple TN2010 Finder section | Explicit expansion intent for that future Finder command | Exact 9.2.2 inheritance; not a rule to uncollapse on every app switch |
| H06 | **C:** standard/user zoom state, Scholar S15 | Future content-aware zoom in owned applications | Exact target fitting algorithm; ordinary Linux apps expose no universal ideal content size |
| H07 | **C:** activation differs from editing, Scholar S12 | Future input interception must precede document actions | 9.2.2 click-through exceptions and modifiers |
| H08 | **C/D9:** persistent Finder objects and presentation; Scholar FD records and TN1176 | Separate folder, window, entry and app identities | Flush timing, relaunch/restart/crash behavior and exact defaults |
| H09 | **C:** desktop database concerns associations, not every presentation property, Scholar S18/S19 | Separate native association and presentation stores | Native reference resolution and portable metadata policy |
| H10 | **Adaptation:** no hidden-content keyboard delivery | Collapsed logical window has no client keyboard focus | This is not a verified classic focus contract |
| H11 | **Adaptation:** no implicit launch activation; fallback is highest nonhidden registered group | Initial source-only admission/fallback policy | Later launch tokens, modal/utility exceptions and historical corroboration |
| H12 | **Adaptation:** modern preemptive execution, native permissions and recoverable file jobs | No deliberate system freezing, process suspension or classic memory partitions | Presentation details remain design work |
| H13 | **Adaptation:** a persistent logical Finder owns desktop focus and native directory entries | Background activation clears the client keyboard target; layout survives qualified native file changes | Exact selection/activation rules, file operations, labels, volume semantics and persistence timing remain unresolved |
| H14 | **Adaptation:** native desktop jobs, shell naming dialogs and internal folder/Trash drops | Collision preservation, partial-batch reporting, guarded modal input and recoverable Trash | Drag/copy modifier matrix, error sequence, alert layout, cancellation and Trash history remain unqualified |
| H15 | **Adaptation:** F2 rename, Ctrl shortcut alternatives and extension-preserving duplicate names | Useful native input and Linux filename associations | Not a substitute for verified target keyboard equivalents, Return/Enter rename, creator codes or original duplicate naming |
| H16 | **Adaptation:** parent-centered plain modal chrome, hint-derived grow controls, original monochrome alerts and retained but disabled application menus | A functional native alert keeps its application identity without menu actions bypassing Save/Cancel; old snapshots remain stale after closing | Target geometry, fonts, default-button states, modality classes and allowed modal menu commands remain unresolved; passing native tests closes no Scholar question |

Apple primary anchors: [TN2010, Mac OS 9.1](https://developer.apple.com/library/archive/technotes/tn/tn2010.html),
[TN1176, Mac OS 9](https://developer.apple.com/library/archive/technotes/tn/tn1176.html).
Earlier Apple documentation is sometimes hosted on third-party mirrors; authorship
and hosting/authentication are separate facts.

## U01–U16 carried forward

These IDs refer to the existing Scholar questions. None is closed by new source.

| Question | Architecture implication / next evidence |
| --- | --- |
| U01 — reference components/defaults | Select locale, component versions and a documented reference lineage before default certification |
| U02 — drag operations | Obtain full same/cross-volume and modifier matrix; native DnD mechanisms do not supply historical policy |
| U03 — replacement/copy failure | Specify conflicts, partial completion, locked items, disk-full and cancellation before file-operation fidelity claims |
| U04 — folder/window persistence | Separate close, Finder relaunch, OS restart, crash and external rename/move observations |
| U05 — selection/rename | Establish icon/list/button targets, modifiers and Return/Enter behavior |
| U06 — views/arrangement/labels | Distinguish global defaults, per-window overrides, one-time cleanup and persistent sort |
| U07 — activation/background windows | Observe input consumption, background operations, group ordering and utility exceptions |
| U08 — Finder Window menu | Corroborate 9.1 inheritance, checks, modifiers and collapsed/pop-up windows |
| U09 — zoom/WindowShade | Establish defaults, focus, transitions, fitting, close veto and restoration; still images are insufficient |
| U10 — aliases/metadata | Define alias-like native references; do not equate an original alias with a symlink |
| U11 — Trash/media | Establish original-location, collision, locked-item and busy-eject behavior; preserve native storage safety |
| U12 — appearance/controls | Obtain unscaled references, font roles, dimensions, hit regions and all control states |
| U13 — Control Strip | Confirm stock modules, movement, ordering, menus and persistence; tray adaptation is not historical proof |
| U14 — startup/shutdown | Document reference configuration and cancellation; native inhibitors remain a deliberate adaptation |
| U15 — system tools/help | Establish each component separately; no fake functionality behind copied labels |
| U16 — system model/rebuild | Original scheduling and desktop rebuilding are historical references; original execution compatibility remains excluded |

## Evidence required before closing a claim

The application browser, MIME-based Open With, workspace scope, local Qt style
plugin, action-right MacLayout button order and rejection of unsupported UTF-8
are native implementation decisions. Qt's MacLayout is not proof of an OS 9.2.2
dialog layout. Retaining client-owned GTK/Electron controls is an explicit visible
compatibility compromise. The browser does not recreate Apple's application
loading, Desktop Database, resource forks or original type/creator dispatch.
No uncertainty item is closed by these software tests.

psn_tch also explicitly requested file drags out of folders and standard right-click
menus, with modern convenience taking priority where useful. The native context
action set, Ctrl-copy alternative, background-window right-click activation and
desktop file-drop receiver are deliberate adaptations (M). They are not evidence
that these exact controls, modifiers or action lists shipped in Mac OS 9.2.2.

psn_tch explicitly selected Charcoal, a rainbow Apple menu image and a classic
Control Strip, and requested visible adjustable WindowShade. These are accepted
product requirements (M). Current local font/icon use does not authenticate an
original 9.2.2 resource or factory configuration. The 0–1200 ms speed control,
120/240/600 ms presets, shell-modal Appearance panel and first three strip modules
are native decisions (M/I). U09 and U13 remain open for exact animation curves,
original speed preferences, modifiers, placement, module inventory and persistence.

Record source component, version, language/region, carrier/custody, original topic
location, fixity and completeness. Separate a prescribed behavior from an
observed transition. Authenticate documentation before proposing targeted original
system observations. Reference frames in the archive include non-stock disk
contents and have no authenticated original framebuffer scale; do not derive
factory arrangement or pixel metrics from them.

Native source implementation, builds and tests were subsequently authorized.
They do not close historical questions. No original OS installation, ROM,
historical application, VM/emulator or reference machine has been executed.


psn_tch requested hold-left-button multi-file selection and direct desktop-to-open-
folder drops. The current rectangle, additive Shift/Ctrl convention, Escape
restoration and group-preserving drag behavior are explicit native convenience
choices (M). They do not close U01/U02 or authenticate the exact 9.2.2 selection,
modifier, hit-area or outline appearance. Existing historical evidence remains
read-only and independent from implementation test results.
