# Platinum Reality distribution prototype

Owner: Rocky (ISO, engine and owned-VM harness). Sammy owns monitor policy,
the eventual installer GUI, canonical integration and desktop release gates.

This branch is a VM-only engineering prototype, not a host installer or a
release. Do not write it to a physical device or install it on a real disk.
R1 is the live ISO; R2 is the installer engine, after R1 passes; R3 will be
the complete installed-disk gate. R4 signing/update hosting needs a separate
go-ahead from Sammy. None of this selects a desktop release for psn_tch.

- [ISO build and provenance](iso/README.md)
- [Installer engine and limits](installer-engine/README.md)
- [Owned-VM proof commands](vm/README.md)
- `vm/owned.py`: private disk-copy/SSH/QMP/lease helpers.
- `vm/verify-iso.py`: unassisted UEFI live boot, without a target disk or network.

All privileged work belongs inside an owned QEMU guest. Never run these tools
with host sudo, point them at a real disk, or bypass the shared heavy-work lease.
Before any eventual real reinstall, copy `~/Projects` to external storage.
