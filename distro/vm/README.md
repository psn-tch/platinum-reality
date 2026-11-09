# Owned VM proofs

## Explicit update37 installer adapter

`qualify-install.py IMAGE.iso --gui --gui-version 37 --greeter platinum
--unlock-ui plymouth --repeat-login` selects the separate new-label adapter.
It requires its exact pinned native source (`NATIVE` in `gui-drive-v37.py`);
unknown pins refuse before input. Legacy mode remains the default. The adapter
uses the actual selected Destination Disk pop-up, Select and Start, checks the
summary's account/encryption, and recognizes the final Quit/Restart alert.
It never presses that Restart until the enclosing controller detaches both CDs.
Question recognition requires actual target/loss text and an aligned Cancel /
Erase and Install pair. Where OCR misses a ringed default button, that button's
row or label is read alone; where it reads the occupied path's "1" as "l", only
the exact pixels of the reviewed update37 frame are accepted, for that exact
native installer and engine (Sammy, 2026-11-07; every adapter region was
measured on an actual frame of the new image first). Occupied Return checks preserve the full
disk hash and require the Start summary before any deliberate reopening.

Eight synthetic refusal controls cover the recognizers. Actual image results
(2026-11-07, native 3053688 / distribution c4487f3, ISO 261dcef7…): blank-target
GUI install, encrypted unlock, Platinum password login and Finder Restart plus
second login PASS (install-20261107T063952Z). Source/widget tests alone are
never consent, install or release evidence.
Missing labels or unknown frames refuse rather than guessing a legacy button.

## Occupied-disk cancellation-only control

For an already-qualified image, a separate non-installing safety observation is:

```bash
python3 -I distro/vm/install-proof.py IMAGE.iso --image-engine --gui \
  --occupied-gui --verify-erase-return --erase-return-only --lease-wait 600
```

This creates its own virtual target and synthetic FAT partition. After the
actual erase question is recognized, Return must visibly cancel to the summary
and preserve the whole-disk hash. The controller stops there, verifies the hash
again and shuts down its owned VM. It never reopens the erase question, clicks
Erase, reads an installed system or boots the target. `installed` stays false;
`ok` means only this requested cancellation control passed. Boot/repeat-login,
display/refusal modes and graphical-unlock expectations cannot be combined.
The retained disk is not deleted. This control does not replace the separate
blank-target install/unlock/password-login acceptance or manual frame review.

## New source controls: repeated login and live-copy hygiene

`qualify-install.py IMAGE.iso --gui --greeter platinum --unlock-ui plymouth
--repeat-login` adds a second startup after the first authenticated desktop.
The controller activates Finder, recognizes the actual Special menu and its
Sleep/Restart/Shut Down rows, and clicks Restart. It reuses the real encrypted
unlock and password-login recognizers, with separate `reboot-*` frames and a
`reboot_login` receipt. It never forces a QMP reset, adds installed SSH access,
edits authentication or saves the synthetic credential for later use. The
one-output 1280×800 fixture restriction remains. A missing/ambiguous menu or
missing second unlock/login fails the requested proof rather than passing a
reset in its place. Capture timings remain offsets within the same QEMU run.

The read-only target helper also checks explicit live-copy images for leftover
live identities, passwordless sudo/autologin, privileged installer/cache/cloud
state, enabled live services, source-mode kernel parameters and live-only
packages. It checks the installed machine identity, keyring and normal mirrors
without printing shadow data. Legacy repository-image readback is unchanged.

`repeat-login-checks.py` and `read-installed-checks.py` use synthetic captures
and temporary files, with no VM or power operation. These are **new test
capabilities, not completed acceptance**. Historical receipts below do not gain
reboot or hygiene claims. A real image run and actual frame review are required;
other installed-feature protocol checks remain separate.

## Latest acceptance (November 1)

Canonical update-29 image **cc5ed36d** passed the real occupied-disk GUI control
and full encrypted install in `install-20261101T100912Z`: physical Return alone
cancelled to the summary with the whole 16-GiB disk unchanged, then deliberate
Erase completed installation, settings readback, detached graphical LUKS unlock
and actual Platinum SDDM password login. Run100211Z independently passed an
actual package-write interruption and explicit destructive reinstall on the
same disk, followed by detached encrypted boot and password login. That is
reinstallation, not resumption or preservation of the interrupted system.

Exact image/source/engine pins and remaining physical-install blockers are in
`../docs/physical-install-readiness.md`. The guard stays on. Completed test disk
images were later removed with Lily Hewitt's approval; receipts/logs/frames remain.
Standard VGA and QXL single-screen enumeration now pass; current headless
two-output activation is not qualified. See `../docs/vm-display-matrix.md`.

The sections below preserve the history of earlier images. Their pending/failing
claims apply to those snapshots, not to the independently tested update29 image.

## Earlier acceptance (October31)

Actual graphical R3b passed on the cc19a5f image in
`build/distro/install-20261031T065910Z/result.json`: real Desktop launch,
all GUI pages and confirmation, UK English/London/UK console choices, encrypted
installation, clean read-only installed-settings/theme/marker readback, both
CDs and NIC detached, graphical unlock and actual SDDM password login into the
installed desktop. The confirmation/completion/unlock/login/desktop frames were
visually reviewed (`stages-review.png`, `visual-review.json`). One virtio-vga
non-GL output and OVMF only. The earlier065310Z run was cut off by host logout
and is not counted. Earlier pending notes below refer to their older snapshots.

### Earlier occupied-disk image: acceptance blocked on Return default

```text
qualify-install.py NEW-IMAGE.iso --greeter platinum --unlock-ui plymouth --gui --occupied-gui
```

The corrected-engine image2fce0069 (native6505ef8) actually displayed the
synthetic partition and removal warning. The real Return-only control in
`install-20261031T145729Z` then started erasing its VM destination, violating
the intended Cancel default. That image is **not accepted**. A native modal
fix has offscreen widget red/green evidence, but still needs integration and a
separately pinned rebuilt image. The new run must include
`--verify-erase-return`: unchanged whole-disk hash and a visible summary after
Return, before reopening and deliberately clicking Erase. Never drop this
control merely to obtain a successful install. See `gui-driver-recognition.md`.

`--occupied-gui` requires the real GUI mode. On the new invocation-owned target
it prepares one synthetic FAT partition labelled VM_DATA, then takes the normal
full-disk baseline. All read-only plans/refusals must leave that baseline
unchanged. The immutable engine plan must explicitly name /dev/vda1, vfat and
VM_DATA and warn of removal; an older image lacking these fields refuses.

Before any erase click, actual GUI OCR must show those partition identities
and the complete warning that every listed partition will be removed. A missing
field or warning fails without sending that click. The full real confirmation
frame is retained for independent visual review, which remains required even
after successful OCR. Then the existing installation/readback/encrypted-boot/
password-login gates apply. Synthetic OCR checks do not qualify the live UI.

Run from the clone as an ordinary host user. Every VM uses its checkout's
`build/daily-use-gate.lock` by default. Separate integration clones MUST set
`PLATINUM_HEAVY_WORK_LEASE` to the same absolute lock path as their integrator;
otherwise their locks are independent. No personal home path is compiled in.
There is no host sudo, host shares, device passthrough,
physical disks, user session changes or release selection are involved.

## Next image: Platinum SDDM theme (pending VM qualification)

With the update-24 native package and the corresponding new engine, the target
selects the shipped Platinum SDDM theme. Its package payload is required before
the engine erases anything. The old a6f30e8 R2/R3 image and stock-greeter proofs
are unchanged and do not qualify the new login screen.

```bash
python3 -I distro/vm/qualify-install.py distro/iso/out/NEW-IMAGE.iso --greeter platinum --gui
```

The controller explicitly recognizes the expected theme and test-account name,
retains the real `login.ppm`, and types the synthetic in-memory password once
using the theme's initial keyboard focus. It never uses stock-greeter click
coordinates for this theme. Initial geometry is restricted to 1280x800 or
1024x768; different geometry stops before typing. Default `--greeter stock`
preserves the old reviewed baseline; no automatic theme guessing occurs.

This new mode has only fixture/source checks so far. The next owned install
must include visual inspection of the actual SDDM greeter and installed desktop,
LUKS unlock, detached ISOs, and successful real SDDM password authentication.
No autologin, SSH key, test service or authentication bypass is added to target.

## R1: immutable live image

```bash
python3 -I distro/vm/verify-iso.py distro/iso/out/IMAGE.iso
```

No target disk or network, no keyboard/mouse input. Save and visually inspect
`desktop.ppm`; a readiness marker alone does not establish visual correctness.
The original Oct29 accepted image/proof remain documented in `../iso/README.md`.

## R2: engine, encrypted disk, real password login

```bash
python3 -I distro/installer-engine/checks.py
python3 -I distro/iso/checks.py
python3 -I distro/vm/install-proof.py distro/iso/out/IMAGE.iso --boot-proof --lease-wait 600
```

The controller creates a NEW 16-GiB qcow2 under `build/distro/install-<UTC>`.
It compares full-disk SHA256s after the plan, wrong erase confirmation, and
boot-medium refusal; none may change the target. An immutable fixture CD has
only public key/configuration/engine source. A VM-supplied nonsecret systemd
credential provisions live-only SSH, restricted to the controller's fresh key.
It is neither a baked public-image backdoor nor copied to the installed disk.
Networking allows localhost SSH only; package installation is offline.

A synthetic random password exists only in controller memory, SSH stdin,
QMP input and the guest's normal password-hashing/encryption paths. It is not
printed, persisted as plaintext, put in argv or written to a fixture. Disk
unlock and account login are separate authentication steps using the same
synthetic password, matching the GUI's current default. The installed-disk
boot detaches BOTH CDs and the NIC and injects no test services or auth bypass.
A reviewed, resolution-specific stock SDDM password-field fixture gates login;
unknown greeter text/geometry fails rather than typing blindly. Theme changes
require a new reviewed fixture. Screenshots are PPM files from owned QMP.

`result.json` reports installation, installed-disk boot and password login
separately; `ok` is true only when all requested stages pass. Use `--boot-proof`
for R2 acceptance: install exit 0 alone is insufficient. Every outcome stops
its own VM and releases the lease. Failures retain receipts/screenshots; an
optional `--diagnostic-hold` retains only that VM for at most five minutes.
The default lease wait is zero; a requested wait is bounded (maximum 1800 s).
At least 20 GiB host free space is required before starting another test.

R2 can use a read-only fixture engine on a selected image; R3 instead requires
the actual immutable image engine. Both headless proofs passed on 2026-10-30
with a6f30e8, one virtio-vga output and OVMF (see engine README for receipts).
Actual GUI installation is still pending a corrected executable shortcut and
live theme in the next image. No physical/Secure Boot/BIOS qualification follows.

## R3: the immutable installer image

```bash
python3 -I distro/vm/qualify-install.py distro/iso/out/IMAGE.iso
```

This is the R2 controller with `--image-engine --boot-proof` always enabled.
It verifies the image manifest and actual engine/wrapper hashes, requires the
native GUI/desktop launcher, and executes the real image wrapper for discovery,
planning, refusals and installation. No fixture engine is supplied. Every
requested stage must pass before `result.json` gets `ok: true`. It inherits
all R2 ownership, password, target-disk, cleanup and bounded-wait constraints.
The image's native/distribution source commits are included in the result.
R3b actual GUI interaction remains a distinct pending acceptance milestone.

## R3b: actual graphical installer (runtime proof pending)

```bash
python3 -I distro/vm/qualify-install.py distro/iso/out/IMAGE.iso --gui
```

The same read-only full-disk guard checks run first. A QMP double-click of the owned executable desktop shortcut launches
the shipped GUI; QMP/OCR drives Welcome,
Destination, Information, License/Agree, Account, Install and Erase confirmation.
It supplies the RAM-only synthetic password through the real password fields.
The GUI calls the actual image wrapper/engine through QProcess; no fake engine,
test-answer shortcut, root Qt process or target autologin is substituted.
Success requires the real GUI completion screen AND the detached installed-disk
unlock/password login proof. Screenshots and OCR TSVs are private test artifacts.
Unknown stages or geometry fail. This driver currently targets the English
1280x800 fixture, not arbitrary themes/resolutions/locales.

The launch must use the real Desktop shortcut, owned by liveuser and executable
(mode 0755). The desktop already has a dedicated trusted-launcher path;
nonexecutable workspace .desktop files stay inert. The earlier mode-0644 image
does not meet this launcher requirement. No application security policy is
relaxed and no direct frontend launch is substituted for this proof.

For every live run the controller saves `live-session-logs.tar` and safely
extracts both initial and fallback supervisor/backend logs. The receipt names
the virtual GPU and monitor count. No log is taken from the user's host session.

## Current result

R2 fixture-engine proof and R3 immutable-image proof both passed with encryption,
clean unmount, both CDs/NIC detached, LUKS unlock, actual SDDM password login and
inspected installed-desktop screendumps. Receipts: respectively
`build/distro/install-20261030T093134Z` and `.../install-20261030T093552Z`.
The image was a6f30e8 / SHA256 66d4bc4218c7d8d7244420df024eba80ecdc7aabd735a314762592bf82edad37.
Known live splash placeholders and a mode-0644 shortcut remain in that image;
there is no live-visual or GUI-launcher acceptance. Corrected image and R3b
GUI install, themed encrypted unlock and hardware matrix remain next.

## New-image graphical unlock and non-default GUI choices (prepared)

Use `--greeter platinum --unlock-ui plymouth` for the new image; the default
plain prompt remains explicit for the historical baseline. The graphical mode
recognizes the exact startup-password message before typing once, retains
`unlock.ppm` and masked `unlock-typed.ppm`, then requires real SDDM login.
At most 30 input-free live boot frames are captured during its first minute;
inspect those to establish actual splash visibility, separately from a filled
theme on disk.

With `--gui`, the real account page selects `en_GB.UTF-8`, `Europe/London`,
and `uk` by the immutable engine's advertised order. It verifies each selected
label in its own row. The controller then reads back locale/keymap/timezone,
the welcome marker's owner and modes, and complete target theme through a
temporary **read-only, no-journal-replay** guest mount after installation.
The fixture helper refuses outside a QEMU/KVM live boot, uses fixed /dev/vda2,
closes its mapper, and never recursively deletes a failed mount. It is not
part of the public image or target. Credentials stay in controller memory
and SSH stdin, not on the fixture CD.

These additions pass 16 source/fixture harness checks. New-image runtime
qualification is still pending; old plain-prompt evidence does not qualify
graphical unlock or actual GUI choices.

## Graphical-console control (October30)

The first new-image run, `install-20261030T163927Z`, installed and read back
correctly but failed its requested graphical unlock gate: the VM's virtual
UART caused a plain text prompt. Removing **only that UART** from a reflink
copy of its installed disk, keeping the same UKI and OVMF variables, displayed
the original graphical password dialog. The observed PNG is privately retained
under `build/plymouth-diagnostic-20261030/uart-control/frame-07.png` in Rocky's
clone. No password was typed in that control. Its automated full-frame OCR
false negative is preserved separately from visual review.

For `--unlock-ui plymouth` the installed-boot fixture therefore has **no virtual
UART**; live boot keeps its serial readiness marker. Both CDs/NIC remain
detached. An exact opaque-pixel match to the immutable ISO's original rendered
dialog, at reviewed geometry, gates credential entry. The bitmap font defeated
OCR even after cropping; no fuzzy password-prompt guess is used. Recognition
is checked against actual captured positive frames and one-pixel-negative
fixtures. The displayed dialog is independently inspected. This is fixture
recognition, not an authentication mechanism or an accessibility implementation.

`install-20261030T164937Z` then actually unlocked LUKS and reached the themed
SDDM greeter. Full-screen OCR missed its small striped-bar Log In title, so
that run was stopped through its controller's normal cleanup before typing
the account password. It remains unqualified. The title is now OCR-read from
its reviewed position, retaining the strict Test User/password/title predicates.
Every subsequent run also archives its exact host controller/helper sources.
The next complete run must still prove real SDDM password authentication.

Host controller dependencies include Pillow, Tesseract, QEMU and the existing
ISO tools; no host packages are installed by the controller. Unknown geometry,
wrong reference dimensions and a single mismatched opaque dialog pixel reject
recognition; initial firmware sizes simply wait for the reviewed display mode.

## Image provenance while the engine continues developing

Immutable-image runs now verify engine and wrapper bytes against the image's
full `distribution_commit` **in the local Git object store**, as well as its
manifest and actual live hashes. They no longer require the current mutable
engine worktree to equal an older image. This permits GUI qualification of a
known image while independent engine source improvements continue. A wrong
manifest hash, non-full ref, or unavailable source commit refuses. Fixture
engine R2 runs still use the exact source supplied on their fixture CD.
A real temporary-repository check changes the worktree and proves that the
older image's pinned blobs are still the only accepted hashes.

## Prepared engine refusal scenarios (not yet run in a VM)

The controller now has a separate `--refusal-scenario small|occupied|bios`
mode. It cannot be combined with GUI/boot-proof/graphical-unlock modes and
never installs a target. The only partition/format preparation is on a NEW
invocation-owned occupied-test disk, before the read-only baseline hash.

```text
# After the shared lease is returned; host controller is unprivileged:
install-proof.py IMAGE.iso --image-engine --refusal-scenario small
install-proof.py IMAGE.iso --image-engine --refusal-scenario bios
install-proof.py IMAGE.iso --refusal-scenario occupied
```

`small` creates an 8-GiB qcow2 and requires the explicit minimum-size refusal.
`bios` creates a 16-GiB qcow2 and boots the live ISO under SeaBIOS, requiring the
explicit UEFI refusal. Both send **empty credential stdin**, require exit2 and
a JSON error before any write-step progress, and compare full target hashes.
No credential is supplied even if a broken guard would try to read one.

`occupied` creates one synthetic FAT partition labelled VM_DATA on the newly
created VM disk, settles only that device's udev metadata, then hashes the
whole disk. Planning must return its actual path/type/label, name the selected
whole disk, and warn of partition removal without changing the hash. This
proves engine information, **not GUI confirmation or OS recognition**. Use the
new fixture engine for this case until an image contains its added plan field;
old immutable images should fail the new assertion rather than fake success.
The source/hash is preserved on the read-only fixture; `--image-engine` instead
uses the immutable image's independently Git-verified source.

These additions pass acceptance-predicate fixtures, including changed hashes,
wrong messages, a premature partition step, missing partition details and an
unknown scenario that must call no tools. Total ISO/harness checks:22; engine
checks:29. No small/occupied/SeaBIOS runtime pass is claimed yet. Interrupted
package writes/retry and a fully NIC-absent install remain separate pending
scenarios, not implied by these refusal modes.
