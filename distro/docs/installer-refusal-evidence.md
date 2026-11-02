# Immutable-image pre-write refusals, 2026-10-31

These are actual isolated VM observations, not synthetic command fixtures or
physical-device qualification. The installer remains QEMU/live-media-only.
No host devices, shares, selected release or user session were changed.

Common immutable image:

- ISO SHA-256: `2fce0069b4b5f8a4acf7b20c05ee88414d14b1097d2df2eab44dbf410ef6014f`
- Native source: `6505ef833072e03083bae642565877050bdc294e`
- Distribution source: `2eb095065e36853f729a10ca28c88330ebcc6b6c`
- Actual engine SHA-256: `02dd983a2fa0ba3f47ec22f803c83619b0f367a95d93c1456b5d2f8b4979e1b2`
- Controller source: `6db2208` (`install-proof.py` archived separately in every run).

The controller compared the actual engine and wrapper in the immutable image
with the full locally known distribution commit before invoking them through
the image's live-only sudo wrapper. No replacement fixture engine was supplied.
The only writable destination was an invocation-owned self-contained qcow2.
Each install refusal used the correct explicit destination confirmation but
**empty credential stdin**. No install/write progress stage was allowed.

| Run under `build/distro/` | Fixture | Actual result |
| --- | --- | --- |
| `install-20261031T152300Z` | 8-GiB target, OVMF | Exit 2: “The VM prototype needs a destination of at least 16 GiB” |
| `install-20261031T152405Z` | 16-GiB target with real FAT partition `VM_DATA`, mounted read-only in the guest | Exit 2: “Destination must be a writable, unmounted whole disk with no active swap” |
| `install-20261031T152451Z` | 16-GiB target, SeaBIOS | Exit 2: “The VM must boot the live media under UEFI” |

Every full-disk before/after hash matched. Respectively:

```text
small:    ebfb4ef19ae410f190327b5ebd312711263bc7579970e87d9c1e2d84e06b3c25
occupied: 8ac600dc13893259e4266a834a49667dc2bb188a7a1c3a72f7d28d22cf58bcd7
bios:     07d217ebccc55480b7afa191674ec5da87f2d14efbc04dbc7e40efe345f16776
```

For the occupied case, partition creation/formatting happened **before** the
baseline hash, and the partition was cleanly unmounted before the final hash.
The actual plan named `/dev/vda1`, `vfat`, `VM_DATA` and the partition-removal
warning before the mounted-child refusal. Raw plan, flat/tree `lsblk` replies,
version, mount/refusal/unmount replies and hash outputs are retained. This
confirms the corrected hierarchy behavior described in
[occupied-disk-discovery.md](occupied-disk-discovery.md).

Every result reports `installed=false`; installed boot and password login are
explicitly **NOT TESTED** in these refusal scenarios. These results do not
qualify the image's GUI destructive default: the separate real Return-only
failure `145729Z` remains an installer acceptance blocker. They also do not
establish safe physical-disk identification, hotplug handling, RAID/multipath,
BIOS installation, interrupted-write recovery or active-swap coverage beyond
source fixtures. See [physical-install-readiness.md](physical-install-readiness.md).
