# Legacy BIOS policy and actual VM refusal

The installed-system policy is **UEFI only**: GPT, ESP, systemd-boot and a UKI.
The current Archiso profile still contains a BIOS/Syslinux live boot path.
Presence of those files is not a successful SeaBIOS boot result.

The engine already refuses an install when `/sys/firmware/efi` is absent,
after authenticating the live VM and selecting a valid target but **before
reading credentials or calling installation/disk tools**. Its message is:
“The VM must boot the live media under UEFI”. A source fixture now verifies
that ordering. This does not establish that the GUI presents it correctly.

For any later image, use a fresh owned SeaBIOS VM and blank
16-GiB qcow2. Retain the unassisted live boot screenshot/readiness result or
actual boot failure. If the live desktop runs, invoke the real image engine
with an exact valid target confirmation but no secrets, and require the UEFI
refusal. Hash the entire target before and after. Preserve JSON/stderr and
verify no write, mapper or mount step began. Review the user-visible GUI error
separately; no legacy target should be partially installed before refusing.

Possible outcomes must stay distinct: BIOS live boot works but installation
refuses cleanly; firmware cannot boot the ISO; or installer guard/error UI is
wrong. Only the first is the intended current policy. Do not call all three
“BIOS supported”. No host firmware or boot configuration was changed.

## Actual immutable-image observation, October 31

`build/distro/install-20261031T152451Z/result.json` passed on ISO
`2fce0069b4b5f8a4acf7b20c05ee88414d14b1097d2df2eab44dbf410ef6014f`,
engine `02dd983a2fa0ba3f47ec22f803c83619b0f367a95d93c1456b5d2f8b4979e1b2`,
QEMU 11.1.3, one non-GL virtio output and a new 16-GiB destination.
The command omitted OVMF/pflash and booted the live image under SeaBIOS.
The live readiness predicate completed, and independent visual review of
`live.ppm` confirmed the native menu bar and initial desktop background.
That early frame does not establish a fully loaded Finder or application
usability; no BIOS GUI error dialog was exercised.

The actual immutable-image wrapper/engine then refused installation with
exit 2 and **“The VM must boot the live media under UEFI”**, using the correct
explicit target confirmation but empty credential stdin. No write progress
stage appeared. Before/after full target SHA-256 matched:
`07d217ebccc55480b7afa191674ec5da87f2d14efbc04dbc7e40efe345f16776`.
This demonstrates BIOS live boot plus clean engine refusal in that fixture,
not BIOS installation support. The separate GUI Return-default defect still
blocks acceptance of this image for installation. See
[installer-refusal-evidence.md](installer-refusal-evidence.md).
