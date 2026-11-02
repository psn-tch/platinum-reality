# Boot performance: owned-VM evidence protocol

Status: actual live-VM observations collected; installed system-manager
metrics and matched performance comparisons remain pending. No service has been disabled
or reconfigured. This is independent of the passing encrypted-install proof.

## Collector and limits

`../vm/boot-metrics.py` emits JSON to stdout. It accepts an explicit `--role`
(`live`, `installed`, or `builder`), verifies that role's exact fixture hostname,
then requires QEMU/KVM and refuses containers. These checks prevent accidental
host invocation; they are not proof of VM ownership or a security boundary.
The existing host controller must additionally establish its owned VM, disk,
QMP endpoint, and shared heavy-work lease before executing the collector.

It queries `systemd-analyze time`, `blame`, `critical-chain`, and `--version`
without changing services or configuration. Raw command results, exit statuses,
truncation flags, the first ten blame entries, and collection duration are kept.
An incomplete startup timestamp is retried for a 30-second elapsed budget;
an in-flight command can extend that budget by at most its ten-second timeout.
Later queries have their own ten-second timeouts. A missing tool, failed query,
timeout or truncated output cannot yield `ok: true`. Stored command text is
limited to 64 KiB stdout and 4 KiB stderr; capture itself uses subprocess pipes
and is not a general-purpose memory limit for untrusted commands.

The intended command, **inside the owned guest only**, is:

```sh
python3 -I /run/pr-test/boot-metrics.py --role live
```

`install-proof.py` and `qualify-install.py` now accept the explicit
`--collect-boot-metrics` option. It archives the exact collector, stages it on
the read-only fixture CD, and queries the live guest after its existing fresh-key
SSH is ready. A requested failed/incomplete metrics query stops that proof before
installation rather than manufacturing a timing pass. The actual observation
below exercises this integration; it does not qualify the rest of that install run.

The fixture supplies that script; this is not a public-image backdoor or a
reason to install SSH in the finished system. The installed-system transport
is **not implemented**. It must preserve real unlock and login, without target
autologin, a permanent test service, or authentication bypass. A legitimate
terminal invocation after password login, followed by read-only offline report
collection, is one possible future transport. Do not infer installed timings
from live-image or builder results.

## Actual live observation, October 31

The read-only collector completed in
`build/distro/install-20261031T142421Z/live-boot-metrics.jsonl` and the matching
`result.json`, on immutable ISO SHA-256
`2fce0069b4b5f8a4acf7b20c05ee88414d14b1097d2df2eab44dbf410ef6014f`,
native `6505ef833072e03083bae642565877050bdc294e`, distribution
`2eb095065e36853f729a10ca28c88330ebcc6b6c`.
The fixture used OVMF, one non-GL virtio output, 4 vCPUs and 8 GiB RAM.

| Observation | Actual result |
| --- | --- |
| Guest systemd | 262.1-1-arch |
| System-manager startup | 8.078 s total: 1.396 s kernel + 6.681 s userspace (reported rounded values) |
| `graphical.target` | 6.676 s in userspace |
| Host-observed live desktop predicate | 9.635 s from QEMU start, including polling |
| Collector | 2 timing attempts, 1.567 s collection |
| Longest reported blame entry | `pacman-init.service`, 4.192 s; not a removal recommendation |

Raw time, blame, first-ten entries, critical-chain, version, statuses and
truncation flags are retained; the collector reports `ok=true`. The **overall
installation run failed later** at an unrelated GUI-driver step and is not
accepted. This independent live observation does not turn that failure into a
successful installer run. Later installer retries also collected observations;
none is a controlled before/after experiment or a physical-hardware benchmark.
No service or package configuration was changed based on the timings.

## Interpretation

According to the primary [systemd-analyze documentation](https://www.freedesktop.org/software/systemd/man/latest/systemd-analyze.html),
`time` describes system-manager startup, not an idle or fully usable desktop.
`blame` lists activation durations, which can be misleading when dependency
waits and concurrent work are involved; it also omits some kinds of waiting.
`critical-chain` has related limitations. Neither list alone justifies removing
a unit. Record the actual guest systemd version with every result.

Keep independent host-monotonic observations of firmware start, first visible
live splash, live desktop readiness, encrypted unlock prompt, SDDM readiness,
password submission, and usable installed desktop. Attribute human/controller
password waiting separately from machine work. The collector's own observation
time is not the desktop-ready timestamp.

## Before/after experiment, when the lease is free

1. Freeze the ISO SHA, native/distribution pins, controller pin, firmware,
   virtual GPU, output count, RAM/vCPU allocation and disk/cache mode.
2. Run at least three comparable cold VM starts, with deterministic input and
   the same captured readiness predicates. Preserve every result, including
   incomplete or failed boots; record host contention and cache conditions.
3. Collect live metrics through the existing ephemeral, fresh-key fixture
   channel. Collect installed metrics only after an explicitly reviewed
transport exists. Preserve raw JSON plus actual stage screenshots. The install
controller also records host-monotonic live readiness and installed unlock,
greeter, credential-submission and desktop observations separately. These include
its polling/OCR and input delays; they are not substitutes for system-manager
metrics or a matched performance comparison.
4. Investigate a reproducible delay using unit dependencies and logs from that
   guest. Prepare one narrow distro-only change on a separate branch; retain
   the unchanged baseline and avoid disabling required security, filesystem,
   networking or login services merely to reduce a number.
5. Repeat the same starts and real unlock/login checks. Report individual
   samples and range/median, functional regressions, and scope. Only a matched
   comparison supports an improvement claim.

Do not add a UART to graphical installed-boot measurements: the existing
controlled experiment showed that its presence selected a Plymouth text
console. Console instrumentation that changes the UI invalidates a graphical
before/after comparison. Physical hardware, suspend/resume, Secure Boot and
multi-output performance remain separate qualification work.

The lightweight checks in `../vm/boot-metrics-checks.py` supply synthetic command
results and mocked hostnames. They perform no real system-manager queries,
VM boots, builds, package operations or service changes.
