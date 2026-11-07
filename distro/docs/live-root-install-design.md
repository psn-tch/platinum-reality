# Offline installation from the immutable live root

Engineering decision, November 6: choose option A. This document specifies a
pending implementation; it does not describe a completed installer or authorize
physical installation. Native source stays pinned to update 36 `ee5030f`.

## Evidence and size target

Actual update-34 baseline: ISO 4,014,555,136 bytes, Squashfs 3,450,793,984 bytes,
embedded repository regular files 1,595,236,076 bytes. Its 823 installed packages
sum to 4,408,014,975 uncompressed bytes. The repository's package-byte total
matches the build receipt. These are measured sizes, not update-36 acceptance
or predictions of compression savings.

Removing the repository alone would still leave approximately 2.42 GB before
compression/layout changes. Measure xz and zstd-19 separately, then investigate
rescue/Arch-installation-only packages and duplicated boot payloads. Preserve
Firefox, Konsole, application integration and graphics/Wi-Fi firmware until
dependency/compatibility evidence justifies changes. Final asset must be
strictly below 2 GiB; aim at or below 1.9 GiB.

## Source and destination boundaries

- Copy the read-only `arch/x86_64/airootfs.sfs` from the authenticated actual
  boot medium. Never copy `/`, the mutable overlay, live processes, credentials
  supplied by the test fixture, host shares or another disk's filesystem.
- Verify Archiso's detached `airootfs.sha512` and the actual Squashfs bytes
  before erasing the destination. This is same-media integrity, not a trusted
  signature. Release-level checks bind the whole ISO to its external receipt.
- Hold the checked regular-file descriptor across validation/copy, passing
  only that descriptor to unsquashfs. Refuse symlink/unsupported source paths,
  invalid policies, unexpected accounts and missing target components.
- A versioned immutable image policy identifies the live-copy mode and native/
  distribution/engine/package pins. Compare it with the running engine and
  required source files. Do not put a Squashfs hash inside the Squashfs itself:
  that is self-referential; use the detached checksum.
- Keep the existing boot-device ancestry, mounted/swap/readonly/device-identity
  checks, exact erase confirmation, UEFI guard and QEMU-only guard. Validate
  the source before partitioning; revalidate the target immediately before
  the first write. Never fall back to online installation after a copy failure.
- Existing version-1 repository images retain their tested code path. Unknown
  source modes refuse; they must not silently become repository installs.

## Copy and sanitization

Extract only the immutable root into the invocation-owned new filesystem.
Exclude `boot` so extraction cannot overwrite the mounted destination ESP.
Exclude live homes (`home`, `root`), runtime trees (`dev`, `proc`, `sys`, `run`,
`tmp`), logs/cache/keyring identity and the duplicate repository. Exclude the
live-only privileged engine/wrapper/boot-helper payload; native application
files remain governed by their package. Recreate empty runtime/home directories
with deliberate modes.

Validate the immutable account databases before erasure. Remove only the
known live account/group and its memberships/subordinate-ID mappings; preserve
system accounts and lock root. Refuse extra regular users or reusable image
passwords. Do not call userdel against shared live PIDs, kill a live user, or
reuse a live password/home/session. New user creation remains in the existing
configuration phase.

Remove live-only SDDM/getty autologin, passwordless sudo, permissive SSH config,
host keys, mirror/keyring initialization services, readiness services, archiso
initramfs hooks, volatile logging and live sleep overrides. Reset machine-id,
D-Bus identity, random seed, fstab, crypttab, hostname, resolver and SDDM state.
Remove the local offline-repository stanza from the installed pacman config.
Initialize the target's own package keyring. Package removal of live-only tools
must happen only once the target's normal kernel/initramfs configuration exists,
so removal hooks cannot build an archiso UKI or fail on an empty ESP.

Every cleanup/write uses fixed relative paths under the new target. Refuse
symlink ancestors and unexpected object types rather than following them into
the live environment. Preserve expected leaf links only where deliberately
recreated or unlinked. Do not recursively delete a still-mounted filesystem.

## Configuration, proof and delivery

Reuse the existing locale/keymap/account/password/network configuration and
target-specific root/LUKS identifiers. Generate a fresh machine identity,
fstab/crypttab and UKI command line; retain the original project Plymouth theme,
normal encrypted unlock, Platinum SDDM password login and owner-chosen restart.
Never carry archiso kernel parameters, autologin or test transport into target.

Required controls include corrupt checksum/policy/engine/source rejection
before erase; symlink escape and malformed-account refusals; exact removal of
live identity; unchanged source bytes; unchanged existing destination guards;
and interrupted-copy cleanup without online fallback or success reports.

Actual acceptance requires a newly pinned optimized ISO: R1 unassisted UEFI,
blank-target real GUI installation, occupied-target Return-only cancel with
unchanged disk hash, detached encrypted unlock, real password login, all
installed-feature observations and a normal reboot/login persistence pass.
Fixtures, an image build, successful extraction or source tests are insufficient.
The private release preparer must receive matching provenance/privacy/review
evidence and an ISO below the size limit. Publication and physical-install
readiness remain separate owner-controlled decisions.
