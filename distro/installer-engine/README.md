# R2 installer engine — encrypted VM installation and password login proved

## Immutable live-root mode (new source; image qualification pending)

The November 6 distribution draft adds a second **offline** source, while
keeping the public configuration/secret/progress interface at version 1.
New images declare `version: 2` and
`installation_source: "live-squashfs-v1"` in their immutable image policy.
Existing version-1 images retain repository/pacstrap installation. Unknown
policies refuse; a failed live copy never falls back to a network or repository.

Before erasure the engine verifies the boot medium's detached Squashfs SHA512,
its embedded source/engine/wrapper policy, account databases, installed package
inventory and login/boot assets. It holds the verified source descriptor through
copying. This checksum checks same-media integrity; it is not a release signature.
The running overlay and its users, settings and test transports are not sources.

The copier excludes the ESP, homes, runtime trees, repository and live-only
installer payload. It checks every expected regular file's presence/type/size
against the immutable archive inventory before accepting extraction, then
sanitizes accounts, autologin, sudo, SSH and machine/package-keyring identity.
Existing target account, network, encrypted UKI and greeter setup are reused.
Live-only package removal runs after target kernel/configuration setup, inside
the target with `SYSTEMD_OFFLINE=1`. Mount failure handling remains unchanged.

`python3 -I distro/installer-engine/live-root-checks.py` covers the new source
boundaries with synthetic files and captured commands; `checks.py` retains the
legacy engine contracts. See [the design](../docs/live-root-install-design.md).
These checks do **not** qualify a live-copy installation. All earlier actual
install receipts below used the legacy repository source. A newly pinned image,
encrypted GUI install, detached login and installed/reboot checks remain required.
The QEMU-only guard remains on.

## Existing repository-mode contract and evidence

The source implements Sammy's version-1 disks/plan/install interface. It is
**not ready for physical installation or public deployment**. Root execution
is refused outside QEMU/KVM Arch live media. No host sudo, real disks or host
boot settings are part of development or testing. The encrypted install/detach/reboot/password-login proof passed on
2026-10-30 in an owned UEFI VM. This is a narrow VM result, not physical
hardware or public-installer qualification.

```text
platinum-reality-install disks --json
platinum-reality-install choices --json
platinum-reality-install plan --config CONFIG.json --json
platinum-reality-install install --config CONFIG.json --confirm-erase /dev/vda --progress-fd 1
```

The Python entry is `engine.py`; the live image provides the fixed
`/usr/bin/platinum-reality-install` sudo-n wrapper. That actual image wrapper
passed a separate end-to-end installed-disk password-login run. Config v1
is exactly the work-order schema. No password fields are accepted there.
`install` reads one bounded JSON object from stdin, closed afterwards:
`password`, plus `passphrase` when `encrypt` is true. These values never appear
in argv, temporary files or progress. PAM hashes the account password into the
normal target shadow file. Python memory zeroization is not claimed. Core dumps
are disabled before credentials are read.

`choices --json` is an additive, read-only v1 operation. Under the same live-VM
guard as `disks`, it returns exactly three sorted, deduplicated arrays of plain
strings: `{"locales": [...], "timezones": [...], "keymaps": [...]}`. No disk
inventory, mount, package command, account configuration or secret read occurs.
The GUI can fall back to its older lists when used with an engine without this
operation; no config-version change is needed.

- Locales are the enabled or commented records in the live `/etc/locale.gen`,
  matched against glibc `/usr/share/i18n/SUPPORTED`, plus built-in `C.UTF-8`.
  Prose comments, invalid tokens and conflicting encoding definitions are not
  advertised. Installation writes the selected record's **actual charset**,
  rather than forcing UTF-8 for a locale that declares another encoding.
- Time zones are regular compiled `TZif` data under `/usr/share/zoneinfo`,
  including valid file aliases staying within that tree. Tables, dangling or
  escaping symlinks and traversal names are excluded. Directory symlinks are
  not recursively expanded, preventing cycles and duplicate alias trees.
- Keymaps are installed `.map` / `.map.gz` basenames under
  `/usr/share/kbd/keymaps`, deduplicated and checked against the same identifier
  and containment rules used during installation. This configures the Linux
  **console/initrd** keyboard, including LUKS unlock; it does not establish an
  application's XKB layout or promise every map suits the target's hardware.

All advertised values pass the engine's shared validators on unchanged live
files. Their authenticity comes from the image's package provenance; discovery
does not execute arbitrary maps, compile locales, or validate every byte of an
upstream timezone file. The immutable live image and offline target package set
must still be verified together in the next actual installer GUI proof.

The choices packet's fixture checks cover guard-before-discovery, exact output
shape, deterministic read-only enumeration, all advertised combinations accepted
by validation, invalid/escaping aliases, ambiguous locale encodings, and correct
non-UTF-8 target configuration. These are synthetic source checks, **not a new
live-image or installed-locale acceptance run**.

Discovery resolves the actual `/run/archiso/bootmnt` device through the block
ancestry, marks a boot CD or partitioned USB and refuses it as a target. Unknown
loop/complex backing arrangements fail closed, including unsupported Ventoy
layouts. Only whole writable unmounted disks of at least 16 GiB are accepted;
mounted children/swap, symlink aliases and ambiguous identities are refused.
The exact confirmation string must match before any disk tools or credential
read. The destination dev_t and size are revalidated immediately before writing.
Physical hotplug safety is not qualified; the VM-only guard remains.

`plan` is read-only. Install verifies every offline package SHA256 and required
package availability before erasure. It uses explicit argv-only sgdisk/mkfs,
pacstrap and arch-chroot steps instead of the larger changing archinstall API:
this v1 has one deliberately narrow partition/boot/service policy. The custom
pacman config has only the ISO file:// repository. R1's repo is unsigned;
`Optional TrustAll` is scoped to it. No network package source or hosted update
repository is added. Normal target mirrors remain available for later updates.

The layout is GPT, a 1-GiB FAT ESP at /boot, and ext4 root, optionally under
LUKS2. The installed UKI is /boot/EFI/Linux/arch-linux.efi. Its explicit cmdline
contains only the new target's UUID/mapping; it never inherits archiso options.
The preset leaves the standard mkinitcpio config search enabled so the
sd-encrypt drop-in is actually applied. systemd-boot is installed with
--no-variables and its removable fallback; no NVRAM is changed.

The new account has wheel and password-authenticated sudo. Live autologin,
passwordless sudo, SSH keys, VM test units and displays.json are not copied.
SDDM's [Last] Session preselects the packaged Platinum Reality Wayland session.
Normal SDDM authentication followed by the desktop is the agreed R2 proof.
The next image selects Sammy's packaged Platinum SDDM theme using
`/etc/sddm.conf.d/10-platinum-reality.conf`. Before erasure, offline package
validation requires its three shipped theme files; target configuration also
checks that those files are regular and stay inside the target. An older native
package without the theme is refused rather than silently producing a broken
greeter. The previous stock-SDDM R2/R3 receipts remain baseline evidence; they do
not qualify this new theme or engine. This is real SDDM authentication, without
autologin or an imitation password dialog. IWD/networkd/resolved, Bluetooth and global
user PipeWire/WirePlumber settings follow setup-fresh-arch.

The tested engine baseline deliberately uses the standard visible initrd
unlock prompt, without Plymouth or quiet. The newer native source has a
Plymouth password handler, but it is not enabled by this engine baseline.
Enabling and proving that themed target unlock is a separate pending step.
The Oct30 baseline live ISO also has a known theme-generation bug (unexpanded
new template variables); it receives no splash/boot-visual acceptance.

Cleanup closes only this invocation's target-keyring gpg-agent, mounted ESP,
root and temporary LUKS mapper. A failed unmount retains the workspace and
reports an error; it never recursively deletes through a mounted target and
never uses a lazy/forced unmount to report success. Signal/command failures
also reach cleanup. The original disk contents cannot be rolled back once a
confirmed installation has erased them.

## Evidence so far

- `python3 -I distro/installer-engine/checks.py`: 15 synthetic/source checks,
  no real devices or external commands. They cover boot USB/CD detection,
  fail-closed graphs, target/config refusals, read-only planning, exact erase
  confirmation, stdin-only secret handling, active encryption hooks, target
  configuration and cleanup failure/agent scope.
- VM `build/distro/install-20261029T084758Z` failed BEFORE the engine because
  console provisioning found a black VT2. Target stayed blank. The harness now
  uses documented nonsecret systemd-debug-generator unit credentials, not a
  baked test hook/SSH key in the ISO.
- VM `.../install-20261029T085243Z` passed full 16-GiB target hash comparisons
  after plan, wrong confirmation and boot-medium refusal. Encrypted formatting,
  offline packages, account configuration and UKI generation completed, but
  ordinary unmount failed and the run correctly reported error.
- VM `.../install-20261029T090000Z` repeated that cleanup failure. Read-only
  fuser/proc diagnostics identified the destination keyring gpg-agent. Stopping
  ONLY that agent by its exact target homedir allowed normal unmount and mapper
  close, without force/lazy flags. Original failed receipt is retained; this
  manual causal experiment is not a passing engine run.
- Corrected-engine runs `.../install-20261029T091616Z` and
  `.../install-20261029T092631Z` returned installation `ok`, cleanly unmounted,
  detached both ISOs, unlocked LUKS and reached actual SDDM. Harness errors
  (first a missing observer marker, then an unattended manual checkpoint)
  prevented a password-login verdict. These are **not passing login runs**.
- `.../install-20261030T082440Z` and the one controlled unchanged-image retry
  `.../install-20261030T082847Z` failed BEFORE engine invocation. The desktop
  supervisor's all-display startup timed out; its one-display fallback used
  `managed` before assignment. Black screen, logs and failed receipt retained;
  reported to Sammy rather than modifying his supervisor ownership.
  Both controllers stopped through their normal `finally` cleanup; targets
  remained blank. Further unchanged-image retries are stopped pending his fix.

All fixtures use an owned qcow2 target, no shares, restricted guest networking
(localhost SSH only), fresh SSH key and read-only fixture ISO containing only
PUBLIC key/config/source. Test passwords are generated in controller memory
and sent through SSH stdin. The immutable R1 ISO is unchanged. The current
controller recognizes the real test-account greeter, checks its exact reviewed
1280x800 geometry, clicks its password field and types one synthetic password
automatically. A changed theme/resolution fails instead of blindly typing.
The R2 run used a fixture engine on the updated a6f30e8 image. R3 then tested
the actual immutable image wrapper and engine, with no fixture engine supplied.
The engine bytes were identical in both cases; no target auth bypass or live
settings were copied. Actual GUI installation through the executable desktop
shortcut (R3b) remains pending the next corrected image.

## Passing VM evidence (2026-10-30)

- R2: `build/distro/install-20261030T093134Z/{result,visual-review}.json`.
- R3 actual image wrapper: `build/distro/install-20261030T093552Z/result.json`.
  Both report `ok: true` and `password_login: PASS`; installed desktop
  screendumps were inspected. Full 16-GiB hashes stayed unchanged after plan,
  wrong confirmation and boot-medium refusal. Installation cleanly unmounted;
  both CDs and the NIC were detached before installed UKI boot, LUKS unlock,
  real SDDM password authentication and the native desktop.
- Native and distribution image pins: `a6f30e8ea2e4a569a21c4a191d58eb7e559dd910`.
  Image SHA256: `66d4bc4218c7d8d7244420df024eba80ecdc7aabd735a314762592bf82edad37`.
  Engine SHA256: `69b2db0c1dfdc8c84b3bebb1869ec22ea5d55fb49e3c74cf3e52ccbf30a91736`.
- GPU/firmware: one virtio-vga output, no GL, OVMF UEFI. These fresh runs did
  not reproduce the earlier all-display IPC timeout. Supervisor/backend logs
  are retained; no causal timeout fix is claimed.
- Harness failures before the passing runs remain failures: 092947 used tar
  wildcards incorrectly in create mode, so no engine ran. 093044 was stopped
  before erasure while fixing overly narrow blank-desktop OCR. A regression
  now exercises a real Python tar stream with both initial/fallback logs.

There are 15 engine and 12 harness/source checks. The positive guest evidence
covers this single virtual hardware configuration with a plain encrypted
unlock prompt. BIOS, Secure Boot, physical installation, the GPU/monitor
matrix and the actual GUI installation remain unqualified.

Primary tool references, checked 2026-10-29:
[pacstrap](https://man.archlinux.org/man/pacstrap.8),
[mkinitcpio](https://man.archlinux.org/man/mkinitcpio.8.en),
[bootctl](https://man.archlinux.org/man/bootctl.1.en),
[systemd debug credentials](https://www.freedesktop.org/software/systemd/man/latest/systemd-debug-generator.html).

The helper can wait boundedly for the shared lease instead of overlapping
other collaborators' release tests.

## Graphical encrypted startup candidate (October 30)

The next image includes Plymouth in the offline target set. Before erasure,
the engine checks the live-only helper/templates against their build inventory
and checks the complete original rendered theme. The installer invokes that
helper with `--root <new target> --no-rebuild`; all configuration writes stay
under the new target. It then selects the theme inside that target and the
existing final `mkinitcpio -P` builds its UKI once. No host boot configuration
or native source is changed. Quiet kernel options and UKI splash are included.

The base hook drop-in is `10-platinum-reality.conf`, sorting before the
helper's `platinum-reality-plymouth.conf`, so the inserted Plymouth hook
survives before `sd-encrypt`. The frozen helper currently copies `RENDERED[:-1]`,
which omits `bullet-8.png`; the distro completes every rendered asset in both
live and target themes without changing the frozen helper source.

New accounts receive an empty user-owned 0644
`~/.config/platinum-reality/welcome-pending`, with user-owned 0755 directories.
Sammy's update-25 Help Center consumes it; the frozen update-24 payload does
not yet open it. The live account never receives the marker.

24 engine and 13 ISO fixture checks pass, including actual helper execution
against a temporary target with external commands disabled, effective sorted
hooks, complete asset validation, and welcome ownership calls. These are
source/fixture results. Actual graphical unlock and themed login acceptance
remain pending the new owned-VM image; old plain-prompt passes do not qualify it.

## Existing destinations and refusal fixtures (source candidate)

`plan` now adds `existing_partitions`, copying the selected disk's public
`path`, `fstype`, `label` and `size_bytes` records. A nonempty list adds an
explicit warning that every listed partition will be removed. It does not
mount filesystems or claim to recognize the installed operating system from
an NTFS/ext4 label. Planning preserves the inventory and calls no disk tool.
The GUI must actually display this information at erase confirmation; the
current frozen frontend ignores the successful plan object, so the source
addition alone does **not** qualify the user-visible warning requirement.

New engine fixtures exercise the real install entry: an 8-GiB target and a
legacy-BIOS live boot both refuse before credential reads or writes. The
small-disk message states the 16-GiB minimum; the firmware error says UEFI.
27 engine checks pass. Real small/occupied/SeaBIOS/offline/interrupted VM
runs are pending the shared heavy-work lease. These source checks are not
runtime failure-recovery evidence. Physical-host guard remains unchanged.

## New-image R3 positive result

`build/distro/install-20261030T165420Z/result.json` in Rocky's clone passes
actual immutable-image offline encrypted install, unchanged guard hashes,
configuration/welcome ownership readback, clean unmount, detached CDs/NIC,
graphical unlock with a masked typed passphrase, Platinum SDDM and real
password login into the installed desktop. PNGs and visual-review.json were
inspected. Image SHA256 cc19a5f536d9d36be94275a03cb7d7cabdc53b1aee8170535e5292aec64d5df3;
native e05327e, distro12ff1da, controllerf45dc4a. One virtio-vga/noGL output,
OVMF and an installed-boot fixture with its UART removed. Earlier failures
are retained; this does not qualify GUI installation, physical devices,
BIOS, Secure Boot or the hardware matrix.
