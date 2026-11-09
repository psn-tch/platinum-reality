# R1: native Platinum Reality live ISO

The host builder refuses to create/start its copied VM with less than 20 GiB
free at the checkout. Before receiving a completed ISO, it rechecks the output
filesystem for the actual image size plus an 8 GiB reserve. Refusal preserves
the completed guest image and receipts; it does not delete other artifacts.
These are free-space floors, not a bound on arbitrary guest build growth.

## Status

The November 6 source draft moves acquisition packages to a **builder-only**
`package-repository` directory outside `profile/airootfs`. Official download
signature policy is retained; the unsigned project repository's exception is
limited to that builder cache. Its complete package hashes/provenance stay in
the build receipts. The live and installed pacman configuration uses official
mirrors without builder paths. Installation itself remains offline: the image
policy declares the new immutable Squashfs copy mode. See
[the installer design](../docs/live-root-install-design.md).

This is a changed installation source requiring new image qualification.
Historical R1/R3 receipts used the embedded-repository layout described below;
they do not establish live-copy acceptance. The first size-oriented candidate selects xz Squashfs and GRUB for live UEFI
boot. The installed target retains systemd-boot. The final ISO size and both
boot/install paths still require actual qualification; no sub-2-GiB release is
claimed here.

### Size-oriented boot layout candidate (not yet boot-qualified)

A controlled extraction of the exact update-36 immutable root, excluding only
its package repository, verified all 154,239 regular-file sizes and all 823
package records before recompression. Same four workers and 1-MiB blocks:

| Squashfs compression | Actual archive bytes | Compression seconds |
| --- | ---: | ---: |
| zstd-15 | 1,856,626,688 | 69.028 |
| zstd-19 | 1,768,247,296 | 219.668 |
| xz | 1,704,099,840 | 240.018 |

These are root-archive measurements, not completed ISO sizes or decompression
benchmarks. The baseline ISO additionally has a 256,079,164-byte initramfs and a
17,248,768-byte kernel in ISO9660, plus a 284,164,096-byte appended EFI partition.
The systemd-boot layout duplicates boot payloads in that partition. Upstream
[Archiso's GRUB mode](https://github.com/archlinux/archiso/blob/master/archiso/mkarchiso)
places its EFI loader there and reads the kernel/initramfs from ISO9660. This
motivates trying GRUB for **live media only** before cutting applications or
firmware. The existing profile includes GRUB and its templates; mkarchiso must
validate the builder's installed version and the resulting image must boot.

A separate byte-preserving initramfs experiment found 230,184,960 bytes of
uncompressed early CPIO. Recompressing only its main archive with xz-9e/CRC32
kept the decompressed SHA256 identical but saved only 4,732,544 bytes per boot
copy (31.354 seconds to compress). This supports keeping zstd for initramfs;
it is not a boot qualification or a reason to remove broad CPU microcode.

The candidate keeps the faster existing zstd live initramfs, all packages,
firmware and BIOS Syslinux path. GRUB's normal entry inherits the same Plymouth
arguments as the previous UEFI entry, boots without a menu delay and does not
play an automatic tune. No target bootloader, encryption policy, physical-host
guard or Secure Boot claim changes. Exact image receipts and R1/R3 evidence,
not arithmetic estimates, decide whether this layout is accepted.

R1 live-desktop proof passed on 2026-10-29: an unchanged ISO booted under OVMF
with no network, target disk or manual input. Desktop readiness took **16.112 s**
(guest uptime 12.43 s); QMP screenshot visually inspected. Package checks 36/36,
source checks 7/7. Image: **3,997,827,072 bytes**, SHA256
`a365afa87f93de0c2e93060bfa5392c613dfc155dd392d89f7a6725912e1edff`.
Build evidence: `build/distro/iso-build-20261029T073456Z`; accepted boot:
`build/distro/uefi-20261029T075856Z/{acceptance,visual-review}.json`.
First boot also showed the desktop but its harness rejected a firmware DCS
prefix before the readiness marker; retained as fixture failure at
`build/distro/uefi-20261029T075626Z`. Parser regression added; second boot used
exactly the same ISO bytes. BIOS, physical hardware and installation are
**not tested**. Archiso 91-1's profile comes from the copied builder guest.
See `UPSTREAM.md` and `upstream-manifest.json` for the original inputs.
Never infer acceptance from the presence of an ISO or a successful build alone.

## Commands

Run as the ordinary host user, from this clone:

```bash
python3 -I distro/iso/checks.py
bash distro/iso/build.sh --base /path/to/arch-base.qcow2 --source-commit SAMMY_FIXED_CANONICAL_COMMIT
python3 -I distro/vm/verify-iso.py distro/iso/out/platinum-reality-2026.10.29-x86_64.iso
```

The build takes this checkout's `build/daily-use-gate.lock` nonblockingly.
When coordinating separate clones, set `PLATINUM_HEAVY_WORK_LEASE` to the SAME
absolute shared lock path for every controller and collaborator. A clone-local
lock does not coordinate with other clones automatically. `--base` is required;
`--source-repo` defaults to the checkout containing this script. A busy lock means
no guest starts. `--bootstrap-profile` is an acquisition command for a NEW
profile only; it refuses to overwrite this vendored profile. The builder is
`build/distro/builder`: a separate copy of the supplied self-contained Arch
cloud QCOW2, a sparse 64-GiB overlay, and a fresh SSH-only key/seed. The original
base disk never appears in QEMU's drives. Only localhost SSH is forwarded;
there are no host shares or device passthrough. Root/package operations are
SSH commands inside that guest. The supplied cloud base's legacy GRUB expects `/boot/vmlinuz-linux`; guest-only
`builder-kernel.py` restores that path from the installed kernel after an upgrade.
An initial builder reboot failure is retained separately; this compatibility step
is unrelated to R2's target UKI strategy. A separate reboot proof loaded
Linux 7.2.11-arch1-1 and reached authenticated SSH; see
`build/distro/builder-reboot-20261029/result.json`. Cloud-init's final stage
takes about 85 seconds on this base, within the 180-second SSH deadline. The builder shuts down in `finally`, with
bounded QMP/process cleanup if guest shutdown fails.

The accepted R1 image used native canonical
`0b26b076dd06c160debb975d3d1f3a68615715af` (monitor update20).
New builds require an explicit `--source-commit`; for R3 use Sammy's current
fixed canonical commit (at least 9b683bd, including installer/Monitors/Finder/
Sound Startup). A Git archive of that commit, not the dirty working tree,
supplies the native source. The distribution overlay is exported separately
from this clone's exact HEAD commit, including the engine, profile and guest
build scripts. Commit distro changes BEFORE building. Both pins, both archive
hashes, engine/wrapper hashes and the transported profile hash enter pin.json.
Its own `scripts/package-arch.py` creates the source snapshot and PKGBUILD;
makepkg builds/checks the package as guest user `tester`. The source commit's
timestamp is `SOURCE_DATE_EPOCH`; `--date YYYY.MM.DD` fixes the ISO filename
date. Source archive, package source manifest, recipe and transport hashes
are retained. Upstream Arch package versions are rolling inputs: this is
traceable, **not a claim of bit-for-bit reproducibility**.

Each attempt has logs and receipts under `build/distro/iso-build-<UTC>/`.
Success copies the ISO to `out/` and verifies its transport size and SHA256
before publishing its `.sha256`. An existing final ISO is never overwritten;
a failed transport retains `.iso.part`. Large outputs and VM keys are ignored
build data, not tracked source or release uploads.

## Profile changes from releng

- Packaged Platinum Reality starts through SDDM's existing Wayland session.
  `liveuser` has an empty password and live-only passwordless sudo. Root's
  password is locked, and releng's tty1 root-autologin override is removed. SDDM autologin exists **only in this live overlay**;
  an installed target must create a password and use the normal Log In.
- Graphical default target and zero-timeout UEFI default; BIOS releng path is
  retained with a short timeout, but BIOS acceptance must be separately tested.
- Releng packages plus fresh-Arch EXTRAS, native package dependencies, udisks2,
  Plymouth, Mesa software rendering and systemd-ukify. No NetworkManager.
- IWD configures Wi-Fi and networkd configures wired links; the releng networkd
  WLAN config is removed so it does not compete with IWD's DHCP. Resolved,
  Bluetooth and global user PipeWire/WirePlumber units follow the native setup.
- Cloud-init is masked, SSH is not enabled, and releng speech/wait-online/time-wait-sync units
  are not started. There is no baked SSH key or builder identity in the ISO.
- Plymouth theme comes from the pinned native source. Its icon is generated
  from the project's original drawing with an explicitly empty XDG data
  directory. Rendering refuses imported assets. No startup sound is included. The BIOS menu background is also replaced
  with a 640×480 rendering of the original project icon.
  Plymouth's hook/theme is in the live initramfs. Compression is zstd with
  bounded worker counts instead of releng's expensive xz settings.
- `/opt/platinum-reality/repo` holds an unsigned prototype local pacman repo,
  built with repo-add, containing the full live-package dependency closure
  (including dependencies already installed in the builder). Official repos
  keep their signature policy during acquisition. `Optional TrustAll` applies
  **only to this unsigned local repo**. `provenance.json` records every package
  filename/size/SHA256; the native source and Archiso versions are also recorded.
  This is intended to support R2's offline pacstrap without network downloads.
- A live-only unprivileged status service writes a readiness marker to the
  serial console, based on the packaged supervisor's `session.json`. It sends
  no input, opens no network listener and performs no session control.
- Nothing writes `displays.json`; fresh-user display policy stays with Sammy's
  supervisor. User imports, personal wallpaper/fonts, ROMs and Apple assets
  are not build inputs.

## Acceptance and limits

`verify-iso.py` takes the same lock, makes a separate OVMF variable store, boots
the ISO read-only in an owned QEMU with no target disk and no network, and sends
no keyboard/mouse input. It waits at most 240 seconds for the real session
readiness marker, records guest uptime/elapsed boot time and takes a QMP
screendump. QMP's peer PID/UID must match the process this invocation owns.
The VM always stops. Inspect `desktop.ppm` with an image viewer before calling
visual acceptance a pass. `acceptance.json` deliberately retains
`visual_review: REQUIRED` until a separate human/agent review records it.

R1 does **not** establish installed-system boot, password login, LUKS/UKI
installation, boot-medium protection, physical GPU/Wi-Fi compatibility,
Secure Boot or safe public redistribution. R2/R3 provide the installation
proof later; package signing/hosted updates are reserved for R4. The live
passwordless account is unsuitable for normal installed use.

Upstream references: [Archiso profile format](https://github.com/archlinux/archiso/blob/master/docs/README.profile.rst),
[mkarchiso](https://man.archlinux.org/man/mkarchiso.1.en).
The installed profile version in UPSTREAM.md is authoritative for this copy.

## R3 integration (source prepared; new image not yet qualified)

The refreshed profile supplies `/usr/bin/platinum-reality-install`, which
executes a fixed Python engine via live-only `sudo -n`. The GUI remains
unprivileged; stdin and JSONL fd 1 survive that boundary. The engine is staged
from the distribution commit into `/usr/lib/platinum-reality/installer-engine`.
It is not part of the installed native package and is not copied to targets.
The native pin's tracked installer shortcut is copied to the live user's
Desktop, while the packaged source remains under the installer's share folder.
`/opt/platinum-reality/image.json` records both pins and relevant hashes.

After building with the fixed native source:

```bash
python3 -I distro/vm/qualify-install.py distro/iso/out/IMAGE.iso
```

This automatically boots UEFI live media, tests the immutable image's actual
wrapper/engine (no fixture engine), checks full-disk read-only guards, installs
offline with encryption, detaches both CDs and the NIC, unlocks the UKI boot,
authenticates through real SDDM and saves greeter/desktop screendumps plus
pass/fail JSON. The old accepted R1 image deliberately cannot pass R3 because
it lacks this engine/wrapper/GUI integration. Actual GUI-driven installation
is R3b and remains separate. Do not infer either success from source checks.

Two Oct30 boots of the accepted old R1 image hit an existing supervisor
all-display timeout and single-display fallback initialization bug before
installation. These failures are recorded in the engine README and sent to
Sammy. Rebuilding that same unfixed native pin is stopped; the fresh image
must include his fix. Historical R1 acceptance is retained, not rewritten.

## Pin timing

Both native ref and distribution HEAD are now resolved to full commits before
starting/bootstraping the builder. Archive creation uses only those saved pins.
A temporary-repository regression moves the branch during bootstrap and checks
that both archives still contain the original source. Earlier controllers
resolved the distribution HEAD after bootstrap; their recorded `pin.json` is
still authoritative, but a pre-build note could predict the wrong commit.
The October30 cc19a5f image recorded native e05327e and distro **12ff1da**.

When sharing the canonical heavy-work lease with another gate, use
`--lease-wait 1800` to wait up to thirty minutes rather than starting another
writer or immediately refusing. The default remains an immediate refusal if
busy. Negative, nonfinite or longer waits refuse before resource setup; source
pins are resolved before any permitted wait.
