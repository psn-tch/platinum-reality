# Physical installation: readiness and remaining evidence

**Status, November 1: NOT READY FOR PHYSICAL INSTALLATION. The QEMU/live-media guard stays ON.**
This is a release-readiness record, not instructions to run the installer on
this computer or permission to change a physical disk. Sammy owns desktop
integration and release selection. Rocky owns the distro/ prototype. Public
package/repository signing and physical installation are separate gates.

## What the existing evidence actually proves

| Area | Evidence | Remaining limit |
| --- | --- | --- |
| Immutable-image installation | Update-29 image cc5ed36d, real GUI `install-20261101T100912Z`: every page, deliberate erase, encrypted install, readback, graphical unlock, actual Platinum SDDM password login and desktop | One virtio-vga/noGL output, OVMF, graphical boot fixture without UART; this does not qualify later images |
| Read-only decisions | Full 16-GiB hashes unchanged after plan, wrong erase confirmation and boot-medium refusal in that run | Not physical hotplug, RAID, multipath or every USB boot layout |
| Encryption and startup | Real graphical password prompt, masked typing, successful LUKS unlock and UKI boot | No TPM, recovery, alternate keyboards, physical GPU or Secure Boot qualification |
| Account configuration | cc5ed36d actual GUI UK English/London/UK selections and readback, welcome marker ownership/modes | Keymap currently means console/initrd, not application XKB; a keyboard-only walkthrough remains pending |
| Actual GUI | cc5ed36d physical-QMP Return-only control100912Z cancels to the summary with the full 16-GiB disk hash unchanged; deliberate Erase then completes installation | Earlier destructive-Return failures remain failures. The KDE-helper fix and this exact rebuilt image have passed; arbitrary themes or later binaries are not qualified |
| Existing partitions | Corrected2fce0069 image actually planned and displayed VM_DATA/vfat/vda1 and removal warning; fixture-engine140455Z refused a real read-only mounted child with full-disk hash unchanged | Immutable-image mounted-child refusal152405Z also passed. No installed-OS identification or physical storage breadth is claimed |
| Small disks / firmware | Immutable-image152300Z/152451Z actual 8-GiB and SeaBIOS refusals, correct explicit confirmation, empty secret stdin, full-disk hashes unchanged | Refusal evidence, not BIOS installation or physical disk support; see installer-refusal-evidence.md |
| Host/container guard | Source fixtures refuse root on physical/non-QEMU systems, containers, and QEMU without the real live mount before disk tools | An accidental-use guard, not a boundary against an administrator who changes the environment |
| Offline operation | ISO file:// repository with restricted networking; target boot succeeds with NIC detached | A complete install with **no virtual NIC at all** remains pending |
| Interrupted install | cc5ed36d `install-20261101T100211Z`: hard QEMU termination during actual package writes, explicit destructive reinstall on the same disk, readback, detached encrypted boot and real login | One package-write interruption point; not resumption, preservation of the old installation, interrupted upgrades or every possible power-loss point |
| Trust / delivery | Exact source/artifact hashes; ISO repository and UKI remain unsigned prototypes. Separate expiring VM TEST repo passes actual signing, seven isolated consumer refusals and signed marker upgrade/rollback; see ../repo/README.md | Empty directory root with dependency checks bypassed; installed-target upgrade/rollback boots, update recovery, production key custody, rolling dependencies and Secure Boot remain unqualified |

The original engine omitted `lsblk --tree`, so real flat JSON hid partitions
and mounted descendants. Corrected discovery has actual VM evidence; see
`occupied-disk-discovery.md`. The Return-only erase failure was a product
defect, not an OCR failure. Under the real KDE platform theme, a helper created
a different visible message box and lost Cancel's default. Explicit widget
dialogs fixed that path; see `../../docs/installer-modal-default.md` for the
red/green KDE checks and private rebuilt-image proof. The later canonical
update-29 image is independently qualified by the runs above.

The current VM-qualified image SHA256 is
`cc5ed36dac278e07607a662f3429bcd7ad33ac80e64fb0ef874c5ce5c7ebfcad`:
native source `3ae1f740859445c7d1c01fe1db37dab11f78c81f`, distribution source
`379440bf2551cbf1d0bf50ea53175e4bcc6ba736`, engine
`36da10dfd00aca77f8700211be9c6c4a0de6115e45dcfd6f638375b8fbbd38f3`.
The retry run found an actual partition-table reread EBUSY defect in the earlier
image. Waiting for udev and bounded reread retries fixed it before cc5ed36d's
successful same-disk reinstall; the failed run is retained as 094758Z.
Receipts, logs and frames are in `build/distro/install-*` of the owned distro
review checkout. Completed target disk images were subsequently deleted with
Lily Hewitt's authorization and deletion receipts; their recorded hashes are historical
test evidence, not a claim those deleted disks can still be independently read.

The first graphical test failed because its UART selected a text console.
An unchanged-disk/UKI control with only that device removed showed the graphical
prompt. Later OCR failures are retained as failed runs, not reassigned to the
product or counted as authentication passes. See the VM/engine READMEs for paths.

## Blockers before any physical pilot

1. Preserve the now-passing destructive-Return regression for every candidate
   image. At its actual occupied-disk erase question, send **only Return** and
   require an unchanged full-disk hash plus return to the summary. Then reopen
   and deliberately click Erase, finish the GUI install with non-default choices,
   and read back the installed settings. Complete the keyboard-only walkthrough.
   Display **all existing destination partitions** before final erase.
   Treat device labels as plain text. A filesystem type or label is not proof
   that a particular operating system is installed, nor permission to erase it.
2. Small-disk, mounted-occupied and SeaBIOS pre-write refusals now have actual
   unchanged-disk evidence on 2fce0069; hard-interrupted/retry now passes on cc5ed36d.
   Still run NIC-absent scenarios, plus broader target-identity/active-swap controls.
   Preserve pre/post disk hashes and actual messages for each image under review.
3. Complete the GPU/output matrix and identify unsupported configurations.
   VM graphics evidence does not establish physical laptop graphics, Wi-Fi,
   Bluetooth, touchscreen, external-monitor hotplug, suspend or lock recovery.
4. Establish signed package/update trust, successful upgrade and rollback,
   failed-signature refusals, recovery after interrupted updates, and explicit
   handling of Arch rolling dependencies. Do not publish the test signing key.
5. Validate boot recovery independently: alternate boot media, encrypted-volume
   access, LUKS-header recovery policy and a failed/new UKI. Secure Boot needs
   its own key/enrollment/revocation evidence; see `secure-boot.md`.
6. Review installer target identity across re-enumeration and hotplug. Current
   immediate dev_t/size revalidation is useful in the VM but insufficient
   physical-device evidence. Disk serial/WWN, mounted descendants, active swap,
   holders, RAID/multipath and the authenticated boot-media ancestry need review.
7. Keep the entire-disk scope explicit. This prototype is not a dual-boot,
   shrink/resize, preserve-home, RAID or remote/complex-boot installer.

## Backup and recovery are a separate user-controlled phase

Before a physical pilot, Lily Hewitt must authorize the exact hardware/destination and
have a verified external backup and a tested recovery path. The backup scope
must include personal documents/downloads, browser profiles and sign-ins,
password-store/wallet material, projects, relevant configuration and recovery
information—not only this source checkout. Account for encrypted data and keys
without copying them into source, logs, the ISO or a public repository.

Verification means restoring representative data into a separate safe location
and checking that it is readable, with the original disk intact. Recording a
copy command's exit status is insufficient. Partition/firmware recovery and
bootable rescue media also need verification. No backup, restore, firmware or
physical-disk operation is performed by this document.

## Controlled future pilot proposal

After the blockers are reviewed, a future authorization should name one spare
machine or replaceable spare disk, its exact whole-device identity, backup and
recovery evidence, intended UEFI/encryption policy, and stop conditions. Prefer
hardware whose existing user disk can be physically disconnected. Review the
guard change as a separate source change; never remove it as incidental cleanup.
Release selection and destructive installation stay deliberate user actions.

## Guard limitations

The engine's environment guard does not authenticate every possible QEMU
configuration. An administrator could manually pass a real disk into a VM;
that would defeat the intended isolation even though virtualization detection
says QEMU. The **owned controller** therefore accepts only self-contained
regular qcow2 files inside its own directory, rejects external data/backing
files for installation targets, and exposes no API for host-device passthrough.
Do not describe virtualization detection alone as protection for arbitrary
real disks or against a privileged attacker.

This document changes no host packages, sessions, profiles, firmware, storage,
release selection or configuration. Real VM proofs listed as pending remain
pending until their receipts and screenshots exist.

## November 2: evidence required for updates 30 and 31

The previous update-29 image evidence remains historical. Update30 replaced
separate settings dialogs with one Control Panels window and added input and
energy preferences. Update31 changes keyboard shortcut delivery. Neither
change is qualified by an older installed-target screenshot or a synthetic
QTest key. After Lily Hewitt selects31, pin the exact native and distribution
commits in a fresh immutable image and record these independently:

| Gate | Required observation | Insufficient substitute |
| --- | --- | --- |
| Settings container | Actual Apple-menu route opens one Control Panels window; Monitors is selected in its list and shown in its page | Old separate Monitors dialog or a source-only recognizer test |
| Input preferences | Keyboard and mouse changes persist in target `input.json`; supervisor records `input_applied` after the actual request and again after ordinary login | File existence or an unacknowledged request alone |
| Energy Saver | One-minute setting really powers off the owned display; keyboard/pointer input wakes it and the session remains usable | A timeout in JSON or merely starting swayidle |
| Layout-aware keys | VM keyboard sends Command-Shift-3 and an independently read clipboard contains a decodable current-screen PNG | QTest key injection, stale clipboard image or shortcut signal only |
| Keyboard installer | Every page/control reached by Tab/arrows/Space/Return, actual questions used; Return-only erase refusal preserves the full disk hash; deliberate keyboard erase completes real install and detached encrypted login | Programmatic focus/selection, pointer-assisted navigation, fake engine or omitted disk hashes |
| NIC absent | No NIC device throughout live boot and installation; actual immutable engine completes offline; detached encrypted boot and normal login | Restricted user networking or detaching NIC only after installation |
| Installed R4 | Signature-enforcing upgrade on installed target, detached normal boot/login, deliberate signed rollback and another detached boot/login; complete baseline payload/version comparison | An isolated directory-root transaction with dependency checks bypassed |

The updated Monitors driver recognizes the container/list/heading and refuses
legacy OK/Cancel dialogs. This is prepared tooling with source checks; its new
recognizer still needs review against an actual new-image VM frame. Primary
and On changes apply immediately and use Keep; there is no final OK to press.
Unmanaged "Use all displays" saves immediately but takes effect on next login.

Storage breadth also needs separate negative controls for active swap,
device-mapper holders, RAID/multipath, physical hotplug/re-enumeration and
authenticated boot-media ancestry. Replacing a same-size target between plan
and erase must refuse before any write. Record device identity and complete
pre/post hashes in the owned fixtures; a model/label or `dev_t` alone does not
qualify physical identity. No physical disks are used for these tests.

R4 still needs a signed-database/unsigned-repository-package refusal, wrong
and expired keys, interrupted-update recovery, dependency/ABI compatibility
and production signing-key custody. Keep test keys private and unpublished.
Independent rescue boot, encrypted-volume recovery and restoration from the
external backup must be demonstrated before requesting a physical pilot.
Only Lily Hewitt can authorize that pilot's exact destination and guard change.
