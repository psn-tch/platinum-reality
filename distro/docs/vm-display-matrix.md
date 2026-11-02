# Virtual GPU and output matrix

Status, November 1: the non-GL rows below have booted in owned QEMU VMs.
The successful encrypted-install proof used one non-GL virtio-vga display.
Single-screen enumeration on another adapter does not qualify its installation,
second head, mirroring or persistence.

| Requested GPU | One output fixture | Two-output fixture | Runtime result |
| --- | --- | --- | --- |
| virtio-vga | Full GUI install, detached encrypted boot and login PASS 100912Z | Two connected heads, actual off-monitor checkbox clicked; backend commit fails, usable primary restored 134017Z | One-head install qualified in this VM; two-head activation FAIL |
| Standard VGA | Backend/native one-output enumeration PASS 133308Z; desktop viewed | Two connected adapters; actual off-monitor checkbox clicked, backend commit fails, primary restored 133914Z | Two devices, not one multi-head card; extended layout NOT qualified |
| QXL | Backend/native one-output enumeration PASS 133334Z; desktop viewed | Capacity 2 with private SPICE server and no client exposes one connector only 133551Z; actual Monitors panel opens | Two-head fixture unsupported as configured; no claim QXL can never support two heads |
| virtio-vga-gl | Not attempted | Not attempted | Outside the current no-host-renderer fixture; not a product failure or a pass |

All enabled fixtures use `-display none`, no host renderer or render node,
no physical-device passthrough, no audio, and the same owned disk/lease rules.
QXL adds a SPICE **Unix socket inside the private VM directory**, with no TCP
listener and no guest agent, shared clipboard or file-transfer channel. No
SPICE client is started by these controllers. Record any unavailable head or
QXL backend failure honestly; do not substitute a different GPU and label it QXL.
Private-directory isolation is essential because that fixture socket has no
SPICE ticket authentication. Do not expose or relocate it into a shared directory.

## Actual evidence and limits

All November 1 runs use ISO SHA256
`cc5ed36dac278e07607a662f3429bcd7ad33ac80e64fb0ef874c5ce5c7ebfcad`,
native `3ae1f740859445c7d1c01fe1db37dab11f78c81f`, distribution
`379440bf2551cbf1d0bf50ea53175e4bcc6ba736`, QEMU 11.1.3 and OVMF.
Each run's `build/distro/install-<UTC>` directory in the private distro review
checkout retains its exact command, controller snapshot/hash, output inventory,
per-head captures, backend/supervisor logs and `result.json`.

The two VGA devices both identify as “QEMU Monitor”. Run 133626Z refused the
ambiguous label before input. A narrow controller correction distinguishes
their actual visible enabled/disabled label ink in the two adjacent checkbox
rows; it refuses unknown/ambiguous styles instead of guessing a row. Frame
review and a retained-frame replay precede actual run 133914Z. Both 133914Z and
virtio 134017Z reach the actual enable request but log `Backend commit failed`
and `A connected display has no usable enabled geometry`. There is no Keep
acceptance, extended interaction, mirror or persistence pass. These establish
a failure of the current headless non-GL fixture/backend path, not a general
physical-monitor defect or a claim that every software-rendered setup fails.

Read-only observer checks now compare actual backend/native logical rectangles,
scale, transform, primary identity and acknowledged generation, with bounded,
non-overlapping extended geometry. Names alone cannot pass. Ten source checks
pass, including wrong-geometry/primary negative controls that failed before
the correction. Replaying actual 133308Z/133334Z records passes the stronger
checks. This replay is labelled separately from the observers shipped on those
two original fixture CDs. It does not add UI or installation acceptance.

Desktop and Monitors frames were reviewed. Two requested QMP head captures
are never counted as two usable screens; an allocated head may be black.
No host render node, GL, SPICE client or physical display was accessed.

## Evidence still to collect after the shared lease is free

The existing controllers accept `--gpu virtio-vga|std|qxl --outputs 1|2`.
The default one-output command remains unchanged. `verify-iso.py` runs the
input-free live boot, while `install-proof.py --boot-proof` additionally runs
the selected fixture through installation, detached encrypted boot, and actual
password login. The R3b GUI driver explicitly refuses alternate GPUs/output
counts until their geometry and interactions have been reviewed.

For example, in the ordinary host-owner shell with the shared lease configured:

```text
python3 -I distro/vm/verify-iso.py IMAGE.iso --gpu virtio-vga --outputs 2
python3 -I distro/vm/install-proof.py IMAGE.iso --image-engine --boot-proof --greeter platinum --unlock-ui plymouth --gpu virtio-vga --outputs 2
```

The receipt saves the requested device topology and a separate QMP screendump
for each requested head. Failed head captures are recorded individually.
An allocated or captured head can still be inactive or black. Neither that
capture nor a supervisor readiness flag proves two usable desktop outputs.

For each row preserve the exact ISO/controller pin, QEMU version, command,
live/session logs, every head screenshot, and failures. Inspect login on the
actual primary display before credential entry; unknown prompt geometry must
stop rather than guess. After genuine login, separately open the Monitors
panel and record:

- Reported connector names, active/enabled states and actual modes.
- The two screens' positions, primary selection and visible panel state.
- Extended layout: a window and pointer can cross the boundary and remain
  usable; each head shows the corresponding region rather than an accidental clone.
- Mirroring: explicitly selected state and matching visible content, followed
  by restoration of extended layout.
- Persistence after a normal logout/login, without editing the saved settings
  behind the UI or imposing a fake topology.

The driver can open Monitors and request second-head enable/Keep through actual
UI input, but the current two-output fixtures have not reached a successful
Keep. Mirror, cross-boundary interaction and logout/login persistence drivers
remain unimplemented. Do not mark a row qualified from generic captures. Multi-GPU
VGA, SPICE monitor configuration and the compositor's selected DRM device can
each limit detected outputs; preserve that distinction in the eventual table.

The newer `install-proof.py --image-engine --observe-displays` mode stages a
read-only collector on its fixture CD. Through the live guest's ephemeral SSH,
it reads the actual supervisor topology and the backend's read-only GET_OUTPUTS
IPC, authenticating that Unix peer's PID/UID. Active backend connector identities
must equal the requested count **and** the native topology identities; the native
desktop must acknowledge the same positive generation. A missing head, stale
generation or topology mismatch fails the observation instead of counting device
capacity as success. Per-head captures are taken again after observation.

Observation alone performs no installation or display-setting change. Explicit
`--open-monitors` opens the real panel; adding `--enable-second-display` deliberately
changes only the owned VM's display preference through its actual checkbox/Keep.
Its source checks
mock all sockets and demonstrate rejection of backend-only capacity, mismatched
identities, stale/unapplied topology, non-desktop/inactive connectors, wrong IPC
peers, truncated messages and oversized responses. Neither source checks nor
enumeration qualifies mirroring, installed login or persistence by itself.

## Primary references and local verification still required

QEMU's [standard VGA source](https://github.com/qemu/qemu/blob/v10.1.0/hw/display/vga-pci.c)
defines both `VGA` and `secondary-vga`. Its
[virtio GPU device source](https://github.com/qemu/qemu/blob/v10.1.0/include/hw/virtio/virtio-gpu.h)
exposes the output-capacity property. Its
[QMP screen-capture schema](https://github.com/qemu/qemu/blob/v10.1.0/qapi/ui.json)
requires a device ID when selecting a head. These are source-level design
references, not evidence that the locally installed QEMU build has every device.
Record and validate that build during the owned-VM run; a missing feature is
an unsupported fixture result, not an invented compatibility pass.

`../vm/display-checks.py` exercises exact argument plans, rejection before
process launch, private endpoint selection and partial-capture receipts using
mocked QMP only. It does not launch QEMU or access the host display.
