# Candidate: allow the first display inventory to finish at startup

This is a supervisor review packet, not a selected release. The candidate
itself has not yet passed whole-session or candidate-in-VM qualification.
The controlled VM experiment below tested the same startup-only budget policy
through temporary instrumentation, not this final source patch.

The initial Sway IPC socket can exist while its first GET_OUTPUTS response is
still slow. On a constrained VM, the default two-second read deadline caused
the extended-session startup to fail before any desktop was usable. Giving
exactly that first inventory request a larger budget allowed it to finish.

The candidate adds one explicit startup inventory read immediately after the
owned backend IPC socket appears, before other requests or client startup.
That read has an eight-second deadline. It neither changes display settings
nor retries a command. Every subsequent request, command and subscription
keeps its existing two-second deadline. It adds one idempotent GET_OUTPUTS
round trip and can add up to six seconds to a failed backend startup. No
application, lock, session-selection or output preference policy changes.

## Evidence and limits

All VM evidence is under build/distro in the author's clone. The original R1
ISO is unchanged, SHA256 a365afa87f93de0c2e93060bfa5392c613dfc155dd392d89f7a6725912e1edff.
Each VM had OVMF, virtio-vga without GL, 8GiB, restricted localhost-only SSH,
no target disk, no audio or host device/share. The live supervisor was traced
before SDDM; this is instrumented diagnostic evidence, not ISO acceptance.

- Four-vCPU controls 20261030T095012Z and 095142Z did not reproduce a timeout.
  The first request took1.124s in the first. Both negative expectations remain
  failed/inconclusive controls. The first fixture waited for SSH startup;
  later fixtures start SSH nonblockingly to reduce that timing confounder.
- One-vCPU negative 20261030T095309Z: sequence1, kind3, empty payload timed out
  after2.021272s at the unchanged2s budget; no usable desktop. Initial and
  fallback logs and a screendump are retained.
- One-vCPU positive 20261030T095354Z: same fixture/image/GPU, ONLY sequence1's
  kind3/empty-payload budget changed to8s. It returned after2.213883s and the
  desktop started. All later queries/commands retained2s. Result JSON reports
  causal_positive_observed:true and a separate visual review records the
  inspected desktop screendump.

These controls establish an insufficient first-inventory budget in that
constrained VM. Backend logs mention kms_swrast, but a Mesa-versus-virtio
comparison has not been performed. The older uninstrumented four-vCPU timeout
cannot retrospectively be assigned to this exact request. This is not proof
of a physical GPU/resume defect, and no universal graphics fix is claimed.

The packet's delayed private Unix-server regression sends an actual Sway IPC
reply after2.25s. An ordinary request times out; the startup read succeeds;
another ordinary request still times out. Protocol framing and request type
are checked. The test touches only its own temporary socket and server thread.
It does not launch Sway or connect to a user's compositor.

Before integration: run the candidate itself in the constrained VM, the normal
one/extended-display session qualifiers, and the relevant full release gates.
The helper is a startup synchronization point; review its position before the
login/lock path and preserve the normal IPC deadlines. The user's active
session/release is unchanged by this packet.
