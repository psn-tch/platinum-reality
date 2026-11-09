# Secure Boot readiness review

Research only, 2026-10-30. No signing keys, firmware variables, boot entries or
packages were created or changed. Physical-install guard remains enabled. This
review does not authorize enrollment or claim a Secure Boot acceptance result.

## Recommendation

First prove an **owner-key chain in disposable OVMF**, retaining the current
systemd-boot → UKI layout. Consider sbctl for key/file inventory and ukify for
signing the complete UKI. Do not turn enrollment into an unattended installer
step. A public installer with factory-trust compatibility needs a separately
maintained shim/distribution signing policy; copying somebody else's shim is
insufficient evidence of a supported chain.

This is engineering judgment: owner-key OVMF controls give the smallest
independently reviewable proof without committing to a distribution signing
service or changing anyone's firmware. Actual hardware support remains a later,
explicitly authorized phase, selected by Lily Hewitt.

## Current project evidence

The engine creates a target-specific UKI at `/boot/EFI/Linux/arch-linux.efi`,
embedding the new root/LUKS identifiers. It installs systemd-boot with
`--no-variables`. It currently supplies no project signing key or enrollment
policy. The passing R2/R3 installations used the ordinary owned OVMF fixture and
did **not attest Secure Boot enforcement**. Their encrypted unlock and SDDM
password-login results therefore cannot be reported as Secure Boot results.

The new work must leave those baseline receipts intact and record its own
source, package, firmware-code/variable hashes, trust certificates and results.
The development host has systemd 262.1, edk2-ovmf 202608 and QEMU 11.1.3; those
read-only version observations are not versions attested inside a new guest.

## What must be authenticated

The UKI combines the stub, kernel and optional initrd/cmdline in one PE image.
A signature can cover that assembled image. Secure Boot changes how the stub
accepts external command-line input; embedded configuration and companion
credentials/add-ons therefore need explicit review in our exact build.
[systemd-stub, upstream manual 262](https://www.freedesktop.org/software/systemd/man/latest/systemd-stub.html)

Ukify accepts a Secure Boot private key and certificate for whole-image signing;
its PCR-signature settings serve a different purpose. Record both build inputs
and the final signed image hash. A syntactically valid signature alone does not
prove that the firmware trusts its signer.
[ukify, upstream manual 262](https://www.freedesktop.org/software/systemd/man/latest/ukify.html)

Every executable in the selected path matters: firmware → systemd-boot (or
removable fallback) → UKI. Test both normal and fallback paths. Systemd-boot
discovers UKIs in `EFI/Linux` and has its own update and optional enrollment
behavior, so signing only the initial UKI is not an update design.
[systemd-boot upstream reference](https://github.com/systemd/systemd/blob/main/man/systemd-boot.xml)

Engineering limits: this does not verify our writable ext4 root, stop a process
already running as root, or automatically supply anti-rollback. LUKS unlock,
package signatures, filesystem integrity and TPM policies remain separate
questions. We should keep password-based unlock for the first proof rather
than add TPM enrollment and recovery to the same experiment.

## Candidate trust strategies

| Strategy | Useful property | Project cost / unresolved requirement |
| --- | --- | --- |
| Owner keys + signed boot manager/UKI | Small chain aligned with current layout | Firmware setup/recovery differs per machine; local signing and key backups needed |
| Factory-trusted shim + owner MOK | Potentially retains factory trust roots | Exact shim provenance, enrollment UI and second-stage verification must be proven |
| Distribution-operated signing + maintained shim | More consistent installation policy at scale | Signing custody, review/certification, updates and revocation become an ongoing service |

Sbctl documents generation of PK/KEK/db keys, enrolled-key inspection, file
signing and key export/rotation. It also warns that replacing trust without
retaining certificates required by firmware/Option ROMs can make hardware
unusable. Retaining required OEM/Microsoft trust is a machine-specific decision;
never use a blanket force option as the solution to an enrollment refusal.
[sbctl upstream manual, retrieved 2026-10-30](https://github.com/Foxboron/sbctl/blob/master/docs/sbctl.8.txt)

Shim validates its next executable and provides verification protocols to later
stages. A signed first stage is insufficient if a later stage fails to enforce
the intended trust. Do not assume an arbitrary systemd-boot/shim combination
works merely because both binaries are signed.
[shim upstream description](https://github.com/rhboot/shim/blob/main/README.md)

SBAT adds signed component/generation metadata for revocation. Shipping shim
would require maintained metadata and a policy for previously signed vulnerable
generations, not just a reusable certificate. Exact accepted artifacts and
current revocations must be checked when building a candidate.
[shim SBAT design](https://github.com/rhboot/shim/blob/main/SBAT.md)

## Keys, updates and recovery: proposed policy

These are design requirements, not implemented features:

- Never place private keys in Git, an ISO, a package, test evidence or public
  export. The first proof uses throwaway **VM-only** keys and its own writable
  OVMF variable file. Production keys must never reuse those test keys.
- Separate boot-signing identity from pacman repository signing identity. Keep
  enrollment authority offline where practical. Document who may sign, rotate,
  revoke and recover before offering a production workflow.
- Build/sign/verify a new UKI under a versioned name; retain a known working
  signed version and recovery media. Do not overwrite the only working image
  before verification. Budget ESP space and interrupted-update behavior.
- Kernel, initrd, systemd-boot and fallback-path updates must preserve the chain.
  A signing failure must report an unusable candidate, not silently select an
  unsigned boot image. Recovering an older signed image is not a guarantee of
  security rollback resistance; revocation may intentionally forbid it.
- Recovery instructions must include firmware key restoration and an independent
  route to the ESP. Firmware trust backups cannot replace firmware-specific
  recovery validation. The installer must not replace another OS's ESP or trust
  settings as an implicit side effect.

## Smallest meaningful VM experiment

Use a fresh disposable QEMU disk and an independently copied writable OVMF VARS
file. Audit the selected firmware build's Secure Boot/SMM requirements first.
No host efivars, disks, firmware services, TPM or devices are exposed to it.
Key/enrollment/signing commands execute **only inside that owned guest**.

1. Preserve existing unsigned-boot baseline and all current receipts.
2. Record firmware variables and effective enforcement state inside the guest.
   Merely seeing a Secure Boot-capable firmware file or leaving Setup Mode is
   not a pass. Capture the actual trust list and enabled/disabled state.
3. With enforcement on, demonstrate that the unsigned target path is rejected.
4. Enroll only the throwaway test authority in the guest VARS, sign the boot
   manager/fallback and UKI, then prove LUKS unlock and real SDDM login with all
   installer media detached. Retain actual screenshots and hashes.
5. Independently tamper with a covered UKI byte, omit a signature on each boot
   stage, and sign with an untrusted test key: each candidate must be rejected.
6. Exercise a signed kernel update, interrupted update, older permitted recovery
   image and failed-signing path. Confirm no unsigned fallback is accepted.
7. Stop the guest normally and preserve evidence plus nonsecret certificates.
   Destroy only owned test secrets through the reviewed guest cleanup policy;
   do not claim memory or storage zeroization from ordinary file deletion.

**Acceptance remains open.** No Secure Boot VM, signing, enrollment, TPM test or
physical-machine change was performed for this document. Public firmware/driver
compatibility, shim distribution eligibility, current certificate transitions
and real-machine recovery remain explicit research gaps.
