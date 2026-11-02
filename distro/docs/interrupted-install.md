# Interrupted package writes and retry: owned-VM protocol

Status: prepared end-to-end controller and synthetic checks only. **No power cut, interrupted
installation or same-disk retry has been executed with this packet.** Existing
successful installations do not establish interruption recovery.

## Why a package-stage event is insufficient

The engine reports `step: packages` immediately before invoking pacstrap. Killing
the VM on that event alone could happen before any package writes. The new
`../vm/package-write-probe.py` instead requires, in the guarded root live QEMU/KVM
fixture, exactly one mounted installer target, a running pacman process, and
nonempty regular package-description files under that target's local database.
It reads only metadata and returns bounded sample identities/sizes. It never
signals processes, writes the target, reads credentials or starts an install.

This observation is meaningful for the **first attempt on a freshly created,
blank invocation-owned target**. Existing package records on a reused target
could predate the current attempt; the probe alone cannot attribute their age.
The host must retain the fresh-disk baseline and actual engine progress, then
query the probe while package installation is still active. If configuration
or success arrives first, record a missed injection window rather than claim
that interruption was tested.

## Abrupt termination primitive

`Guest.power_cut_install_fixture()` accepts only that Guest object's active
Popen, matching PID, live-ISO mode and validated installation-target path. It
checks the QMP endpoint's socket type/owner and authenticates the actual kernel
peer PID/UID through the existing QMP method. QEMU must report running. It then
sends SIGKILL to that owned Popen and requires exit -9. It does not stop a host
session, use a saved arbitrary PID, invoke host sudo or delete the target.

SIGKILL can leave the Unix endpoint behind. After waiting for process exit,
the method removes only the same owned socket inode; a replacement is retained
and produces an explicit failure. No directory traversal or recursive cleanup
is used. The result deliberately leaves `package_write_stage` requiring caller
evidence; a successful kill is not proof of the required write stage or retry.

This simulates abrupt **VM process termination**, not physical loss of power to
storage. Host caching and already-completed asynchronous writes differ from
hardware power-loss behavior. Do not extrapolate either filesystem integrity
or transaction atomicity from this experiment.

## Prepared end-to-end controller, not yet run

1. After the shared lease is returned, create a new self-contained target using
   the existing owned controller. Freeze ISO/native/distribution/controller pins,
   save the fresh full virtual-disk hash, and run the normal read-only refusals.
   Keep credentials only in controller/guest memory and normal stdin pipes.
2. Archive exact helper sources and place the probe on the read-only fixture
   CD. Start the real immutable wrapper/engine asynchronously, retaining JSONL
   and stderr separately without logging credential input.
3. Observe the actual package progress event, then require the probe's live
   writer plus nonempty package metadata on this first blank-disk attempt.
   Save that JSON and a stage screenshot before calling the guarded primitive.
   Every poll and subprocess needs a deadline; a missed window fails this test.
4. Wait for the owned QEMU and transport to exit. Retain logs/screenshots and
   the **same target file**. A hash of qcow2 container bytes is not a hash of all
   virtual sectors; label these differently. Preserve the interrupted target
   as an evidence copy before any retry, while ensuring adequate free space.
5. Boot the live ISO again with the same target, preserving identity and using
   fresh per-phase logs. Discover and plan again. Record the partial partitions
   and a new explicit entire-disk erase confirmation; retry is a destructive
   reinstall, **not resumption or repair**. Never quietly create a new blank
   target and call that same-disk recovery.
6. Reinstall through the actual engine and require success plus clean unmount.
   Verify locale/keymap/timezone and target metadata with the existing read-only
   readback helper. Detach both CDs and the NIC, then require graphical encrypted
   unlock, actual SDDM password authentication and a usable installed desktop.
7. Separate verdicts: write-stage observation, abrupt termination, evidence-copy
   preservation, same-disk identity, explicit retry, clean completion, detached
   boot, password login and visual review. The overall result passes only if
   all required stages pass. Retain and report every failed attempt.

`install-proof.py --interrupt-retry` now wires that protocol through
`interrupted_retry.py`. It requires encrypted `--image-engine --boot-proof`,
one virtio output, and no GUI/refusal/display-observation scenario. The first
plan must explicitly report an empty partition list on the invocation-created
blank disk. An asynchronous SSH Popen passes the synthetic credentials through
stdin only; its command/log paths contain none. It requires a current packages
progress event, an active writer and real written package metadata, then
rechecks progress before invoking the authenticated VM-only kill primitive.

The same destination's path/device/inode must survive the cut and restart.
`target-interrupted-evidence.qcow2` preserves the post-cut container with an
exact matching **container-byte** hash; this is not a virtual-sector hash.
First-attempt serial/QEMU/command/session logs move into
`interrupted-first-attempt/` before the next VM start can truncate them.
Low host space, an identity change or failed evidence copy stops before retry.
The new live boot must plan the interrupted partitions before explicitly
confirming a destructive reinstall onto `/dev/vda`. Full readback, detached
graphical encrypted unlock and actual password login are required for overall
success. There is no partial pass from the kill helper or the reinstall exit.

Command shape for the separately reviewed future owned-VM run:

```text
install-proof.py QUALIFIED-IMAGE.iso --image-engine --boot-proof --greeter platinum --unlock-ui plymouth --interrupt-retry --lease-wait 0
```

**No actual interruption or same-disk retry has run yet.** Do not call the
primitive from generic cleanup. Before execution, independently review the
new controller and use the next accepted native/image pin after the destructive
Return-default gate. This source preparation does not waive that blocker.

`../vm/interruption-checks.py` uses synthetic directory trees and mocked process,
socket metadata and QMP methods. It covers missing/stale stage evidence,
unmounted/ambiguous targets, non-owned or stopped processes, QMP ownership/type,
call ordering, replacement-socket retention and non-SIGKILL exits. It sends no
real signal, boots no VM and provides no recovery verdict.

`../vm/interrupted-retry-checks.py` adds ten synthetic-file/mocked-process checks
covering progress completeness, active-writer/metadata requirements, missed
windows, progress advancing during the probe, secret-only stdin, bounded owned
SSH cleanup, target identity, exact evidence-copy preservation, low-space
refusal and early rejection of unsafe CLI combinations. No real VM, SSH,
package installer or signal is used by those checks. The existing seven
interruption-primitive checks and 23 ISO/controller checks also pass on the
prepared source. Runtime stage attribution and real recovery remain pending.
