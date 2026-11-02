# R4 signed repository prototype — isolated VM evidence

**November 1: actual producer and isolated consumer trust/upgrade/rollback
PASS in the owned builder. Installed-desktop boots and broader update/recovery
qualification remain separate gates.** This is a disposable-VM test design, not a release
signer or public update service. Nothing publishes, changes the host keyring
or selects a release.

## Actual producer milestone

`build/r4-first-runtime-20261101/run-20261101T134221Z` in the distro review
checkout records actual unprivileged execution of both helpers inside the
noncontainer `pr-iso-builder` QEMU/KVM VM, under the shared lease:

- Baseline native `3ae1f740859445c7d1c01fe1db37dab11f78c81f`, package SHA256
  `2584d1df57fd49707e55c8b6f8c2e5bdd98de45cb61b6eac807a40e07f111cbc`,
  unchanged after the experiment.
- Actual makepkg marker repack `0.1.0.r4vmtest-1`, package SHA256
  `413b9dd2ca75f8677f009b80fb359afd68856eb92e04dc7daf66b27802f81c14`.
  Every one of the baseline's 63 payload entries passed the independent
  bytes/mode/directory comparison; only the explicit VM TEST documentation
  marker is added. No fresh native compile/check or dependency qualification.
- Fresh one-day Ed25519 VM TEST key
  `079AC45EE7DF5B78D892CFB2FCF3EB79B5BC70BE`; both packages and both
  repository databases were actually signed and verified in the producer.
- Only `public/` was transferred. Its allowlisted inventory and every file
  hash/link were independently checked after transport. Public-only archive
  SHA256 `ea5e4a4e73e40745f0580a477d3897d921290267884446679d39d97b5c060c94`.
  Neither the signing key nor an agent/keyring was transferred or published.
- Actual guest versions: Python 3.14.9, pacman 7.1.0.r9.g54d9411-3,
  GnuPG 2.4.9 and Zstd 1.5.7-3. Real Zstd archives, makepkg, repo-add and GPG
  now have runtime evidence; the earlier synthetic fixtures remain source tests.

Signing and verification inside the producer do **not** prove consumer behavior.
The independently executed consumer below supplies narrow transaction/trust
evidence, separate from the installed-OS upgrade/boot/rollback milestone.
The expiring key is test material only;
reusing this evidence later does not make an expired key a release identity.

## Actual isolated consumer milestone

Clean runs `consumer-run-20261101T140620Z` and the stricter
`consumer-run-20261101T140919Z`, under `build/r4-first-runtime-20261101/`,
passed actual pacman transactions in a fresh directory root **inside the owned
builder**. Each uses its own local/sync database, cache, config, keyring, log
and hooks directory. A separate disconnected network namespace prevents key
retrieval/network access; public artifacts arrive through the pinned public-only
archive. No normal guest or host package database/keyring is used.

This is deliberately a package-mechanics experiment: **`--nodeps` twice** skips
dependency checks for the empty root. It executes no native desktop and does
not qualify dependency resolution, a complete installed OS, a live-session
upgrade, rolling Arch dependencies, boot recovery or firmware trust.

The observed positive sequence was:

1. Import the exact public fingerprint; its signature is rejected as unknown
   trust until deliberate local certification in the dedicated test keyring.
2. Refresh the signed baseline repository and install `0.1.0-1`.
3. Refresh the signed candidate repository and upgrade to `0.1.0.r4vmtest-1`;
   check the actual marker hash, package version and `pacman -Qkk`.
4. Refresh the signed baseline and explicitly downgrade. Check the restored
   version, removed marker, `-Qkk`, and every installed payload byte/mode/path
   against the baseline state. Local install timestamps may change on rollback.

Seven actual negative controls passed. Every refusal retained exactly the same
installed payload and local package database state taken immediately before it:

| Control | Observed refusal |
| --- | --- |
| Missing public key | Unknown key and PGP-signature error; no network retrieval succeeds |
| Imported but untrusted key | Signature has unknown trust; acceptance follows only explicit certification |
| Unsigned database | Required `.db.sig` retrieval fails because that file is missing; this is signature-required download refusal, not a cryptographic verification claim |
| Corrupt database with old signature | Invalid PGP signature |
| Unsigned local package | Package-specific missing-required-signature error |
| Corrupt local package with original sidecar | Package-specific invalid PGP-signature error |
| Damaged package sidecar with unchanged archive | Package-specific invalid PGP-signature error |

The stricter 140919Z run restores a known-good sync database after database
negatives and before local-package negatives. A prior invalid repository cannot
qualify a package refusal; each requires its own actual package path and
signature error. Earlier setup failures remain failed: an absent producer
workspace after reboot, empty local DB format initialization, empty `-Q` exit 1,
and recognizing the missing `.db.sig` failure as a required-download error.
The repaired consumer uses a fresh workspace and public transport independently
of the producer's private workspace. Actual consumer source and controller
hashes, command/exit logs and public-only log archives are retained per run.

Run 140919Z also requested real builder poweroff and then deliberately injected
an SSH transport exit 255. The repaired owned-VM helper waited for normal QEMU
exit 0 with no QMP quit. This is a controlled transport-error test, not proof of
the cause of the earlier missing producer directory. These VM-harness checks
do not qualify desktop shutdown, physical power loss or interrupted updates.

**Still pending:** a valid signed DB containing an unsigned package (local `-U`
is not that control), independently wrong/expired signing keys, installed-target
upgrade and rollback followed by detached encrypted desktop boots/login,
interrupted update recovery and Arch dependency/ABI policy. The full-OS
milestone below remains required; the physical-install guard stays on.

## Producer candidate

`produce-test-repo.py` refuses root and any environment other than the
unprivileged `pr-iso-builder` QEMU/KVM guest before input reads, directories or
GPG commands. It separately refuses a container layer, even when `--vm` reports
an underlying QEMU/KVM hypervisor. These environment checks prevent accidental
invocation; the host controller must still establish VM ownership and the lease.
Inputs and new output stay in that guest's home. It accepts two
explicit x86_64 `platinum-reality` packages and hash-matching build receipts.
The candidate must compare newer with `vercmp`, and a named **installed payload
file** must actually change or be added. Metadata-only version bumps refuse.

Its proposed key is clearly named VM TEST / NOT FOR RELEASE, Ed25519 signing,
unprotected and expiring after one day, in a new isolated `private/` directory.
It reads no real key or user GnuPG configuration. It signs both packages and
separate baseline/candidate database snapshots. Only `public/` is transferable:
public key, signed artifacts, explicit consumer configs and public inventory.
The key agent is stopped by its exact homedir; private key material never
belongs in Git, transport archives, the ISO or evidence collected to the host.
The actual producer milestone above exercised real GnuPG/repo-add and Zstandard
packages with the recorded Python version.

Thirteen light fixtures pass: host/root and nested-container refusal, VM-role check, archive identity and
proof validation, duplicate/symlink refusal, receipt hashes, full fingerprints,
public/private output separation, no-key refusal for unchanged/downgrade
candidates, and strict consumer configuration. Their archive fixtures are
explicitly **uncompressed synthetic tar**, not built or signed Arch packages.
They do not establish signature enforcement or update success.

## Trust policy and primary references

The producer signs the package first, then the database. `repo-add` can include
a sidecar package signature in its database and separately sign the database.
[Arch repo-add manual](https://man.archlinux.org/man/repo-add.8.en).

The proposed isolated consumer config uses `Required TrustedOnly`, including
local/remote package-file operations, a dedicated test GPGDir/cache, and only
one file:// test snapshot. It never changes the normal pacman.conf. Required
covers packages and databases; TrustedOnly requires a trusted key, rather than
accepting an unknown imported key. This is policy to **test**, not proof of
actual enforcement. [Arch pacman.conf manual](https://man.archlinux.org/man/pacman.conf.5.en).

The VM imports the exact test public-key fingerprint from its pinned transport,
then locally certifies it in its **separate test pacman keyring**. Importing and
trusting are distinct steps. Never fetch by an unverified short ID/UID or alter
the host/normal keyring. [Arch pacman-key manual](https://man.archlinux.org/man/pacman-key.8.en).

GnuPG documents batch/loopback quick generation and an empty passphrase for an
unprotected key. That is chosen ONLY for this expiring disposable test identity;
it is not production key custody. [GnuPG key-management manual](https://www.gnupg.org/documentation/manuals/gnupg/OpenPGP-Key-Management.html).

References reviewed October30; the Arch manual identifies Pacman7.1.0. Record
actual GPG, pacman, repo-add, Python and package versions in the eventual VM
receipt rather than assuming the documentation's version matches the guest.

## Smallest real execution milestone, after earlier queue items

1. Build a reviewed content-changing candidate in the owned builder from pinned
   source/recipe. A test-only installed documentation marker is sufficient to
   test the transaction if explicitly labelled; don't describe that as a new
   desktop-feature update. Retain the original ISO package unchanged, receipt
   hashes, candidate build inputs and a meaningful payload digest difference.
2. Run the guarded producer **inside that builder**, then transfer only its
   allowlisted public/ subtree through the existing authenticated guest channel.
   Audit its inventory. Do not collect private/, GnuPG sockets or agent state.
3. Install the current immutable ISO in a fresh owned target. Prove ordinary
   encrypted boot and SDDM password login first. Keep its original passing disk
   and make a reflink copy for update experiments; retain the password only in
   controller memory for the continuation. No target authentication bypass.
4. Attach public repository data as a **read-only fixture CD**. On the test
   copy only, perform the genuine package transaction using an isolated test
   keyring/config. A guest-only mounted/chroot transaction is an acceptable
   initial scope if labelled offline; it does not prove safe live upgrading.
5. Verify package version, new payload bytes and package file integrity. Detach
   media/NIC, boot that upgraded copy and authenticate into the real desktop.
6. Deliberately switch to the signed baseline snapshot and request a downgrade.
   The original package must also be signed by the test key; no unsigned rollback
   exception. Check restored version/bytes (or removal of the added marker),
   then repeat encrypted boot, real authentication and desktop verification.
7. Preserve independent installation, upgrade, boot-after-upgrade, rollback and
   boot-after-rollback verdicts, commands, public hashes and screenshots. No
   component gets a PASS from a package producer exiting zero.

A repository-only transaction on fixed dependencies does not qualify Arch
system-wide rolling upgrades. Real deployment needs dependency/ABI policy,
transaction recovery, a reviewed release key/custody/rotation/revocation plan,
source/license availability, hosting and client trust bootstrap. Package PGP
signing is separate from UKI/Secure Boot signing.

## Required negative controls

| Control | Required evidence |
| --- | --- |
| Unsigned database | Required policy rejects refresh; installed package unchanged |
| Corrupted database with old signature | Verification fails; no package transaction |
| Valid signed database containing an unsigned package | Package is refused; installed version/payload unchanged |
| Imported but untrusted test key | Rejection before local certification; acceptance only after the deliberate trust step |
| Wrong/unknown signing key | Refusal, without importing/trusting arbitrary keys |
| Missing/expired test key | Explicit refusal; no automatic insecure fallback |
| Unchanged or older candidate at producer | No keys/output generated (fixture-covered); deliberate consumer rollback stays a separate action |
| Interrupted upgrade | Preserve disk/state; document recovery, then verify before calling it safe |

Use fresh owned download/cache/sync state for negative controls so a previous
valid cached artifact cannot mask a bad repository. Check the installed package
DB and payload before/after; downloaded sync caches may legitimately change.
Do not equate a checksum mismatch with a demonstrated signature failure.

Signatures alone do not establish repository freshness or prevent a freeze/
replay of an older valid catalog. Deliberate rollback is an explicit test action;
it is not permission for silent downgrades in a future daily-use updater.
Real anti-rollback/freshness policy remains an unresolved production requirement.

## Resume command shape (guest only, not executed)

```text
python3 -I produce-test-repo.py
  --baseline OLD.pkg.tar.zst --baseline-receipt OLD-build.json
  --candidate NEW.pkg.tar.zst --candidate-receipt NEW-build.json
  --proof-file usr/share/doc/platinum-reality/r4-vm-test.txt
  --output NEW-GUEST-WORK-DIRECTORY
```

The file/receipt paths must already be regular files inside the owned guest's
home. This source packet does not build a candidate, install a consumer keyring,
run pacman, boot a VM or offer a host-facing signing/deployment command.

## Marker-candidate builder (actual VM repack passed)

`prepare-vm-candidate.py` fills the first producer input step without pretending
that a package-version change is a desktop-feature update. It requires the
unprivileged noncontainer owned builder **before reading inputs or running
makepkg**, and accepts only a receipt-matched named x86_64 baseline package.
It creates a fresh guest-home work directory and an explicitly local-source
recipe, preserves the baseline's runtime dependency/license/association metadata,
and adds `usr/share/doc/platinum-reality/r4-vm-test.txt` containing **VM TEST / NOT
A RELEASE**. No existing program/data file is rebuilt or intentionally changed.

After makepkg, it independently compares every baseline payload path, byte hash,
mode and directory with the candidate. Only package metadata and the explicit
new marker are excluded. New unrelated files, changed program bytes or modes,
unsafe archive paths, links/special files, install scripts and ambiguous package
identity refuse. This initial fixture deliberately supports only the current
regular-file payload; an unsupported baseline fails instead of weakening checks.
The candidate version must compare newer using actual guest `vercmp`.

The generated recipe has no native build/check function, no URL sources and no
install step. `makepkg --nodeps --nocheck` is appropriate only to this controlled
payload repack: it does **not** qualify dependency resolution or a fresh native
build. Its isolated HOME/config/data/cache/TMP paths and output configuration
avoid the builder user's normal settings. It does not install packages, generate
keys, sign anything or change pacman's configuration. The later actual signed
consumer transaction, boot and rollback remain separate gates.

Guest-only future command shape:

```text
python3 -I prepare-vm-candidate.py --baseline RECEIPT-MATCHED.pkg.tar.zst --baseline-receipt native-build.json --output /home/tester/r4-marker-UNIQUE
```

Use its `candidate-build.json` as the candidate receipt for the existing signer,
with the exact package path and proof-file name above. Transfer only the signer's
allowlisted public output, never its private keyring or this whole builder home.

Seven `candidate-checks.py` checks pass with synthetic **uncompressed** tar
fixtures and mocked guards; the recipe receives Bash syntax checking without
execution. They do not establish real Zstd input, makepkg output, signature
verification, installation, upgrade or rollback. The separate actual marker
build above now supplies real archive/makepkg evidence; it does not turn these
synthetic checks into consumer or boot evidence. The producer's 13 source
checks also pass.
