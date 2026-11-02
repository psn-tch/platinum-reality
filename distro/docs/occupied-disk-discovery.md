# Occupied-disk discovery: runtime defect and correction

October 31, 2026. This is an installer-engine issue, not a change to the running
native desktop. Physical-install guards remain enabled.

## Observed failure

The first occupied-disk GUI proof on immutable distribution `6505ef8` stopped
before any GUI erase confirmation or credential entry. The controller had
created and formatted one synthetic 1-GiB FAT partition, `/dev/vda1`, labelled
`VM_DATA`, on its newly created disposable target. The actual image engine's
plan nevertheless returned `existing_partitions: []`. That run is retained as
failed: `build/distro/install-20261031T140026Z/` in the private test checkout.

The engine requested explicit PATH and other columns from `lsblk --json`, but
neither NAME nor `--tree`. The primary [lsblk manual](https://man7.org/linux/man-pages/man8/lsblk.8.html)
documents that the JSON child hierarchy requires NAME or `--tree`. Without it,
the engine's descendant walker saw the partition as an unrelated top-level
entry. This also prevented its intended mounted-descendant busy check from
seeing that partition. A synthetic preconstructed tree had hidden the real
command-shape defect in earlier source checks.

## Narrow correction and causal evidence

`5ae599c` requests `--tree` explicitly without changing the selected columns,
destination rules, confirmation, credentials, or physical-install guard.
Two new command-adapter regressions fail on the old implementation: partition
details disappear, and a mounted child is not refused. Both pass after the
correction; all 32 engine checks pass. The retained red fixture output is
`build/artifacts/lsblk-tree-fix-20261031/red-fixtures.txt`.

Actual fixture-engine run `install-20261031T140403Z` passed on the same original
live ISO. Its util-linux version is **2.42.4**. Retained flat output shows vda
and vda1 as siblings; the otherwise identical tree query shows vda1 under vda,
with the real `vfat` type and `VM_DATA` label. The corrected engine includes
those identities and the removal warning in its plan. The complete virtual
target disk hash is unchanged before/after the read-only proof.

The stronger controller `ceea00a` passed actual run `install-20261031T140455Z`:
after checking that plan, it mounts the synthetic FAT partition **read-only**
and invokes installation with empty credential stdin. The engine exits 2 at
`validate`, refusing a destination with a mounted descendant before any write
step. The fixture is unmounted, and the full 16-GiB virtual-disk hash remains
unchanged. The engine bytes in both passing fixture runs have SHA256
`02dd983a2fa0ba3f47ec22f803c83619b0f367a95d93c1456b5d2f8b4979e1b2`.

## Remaining acceptance

These are **R2 fixture-engine** results. They do not repair or qualify the old
immutable ISO. A separately built image containing the corrected engine must
pass the occupied-disk GUI warning screenshot, actual GUI installation,
installed-setting readback, detached encrypted boot, real SDDM authentication,
and desktop visual review. Keep the earlier image and failed receipt under
their original hashes. No physical hotplug, complex device-mapper/multipath,
USB boot or arbitrary filesystem support follows from this one FAT/QEMU case.
