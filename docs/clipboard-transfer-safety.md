# Compositor-owned clipboard transfer safety

This is source under review, not a selected desktop release. The earlier
71-case candidate stays blocked. Its passing ordinary workflows did not cover
backpressure or an early reader close.

## Scope and mechanism

Qt 6.12.0's [retained selection implementation](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/wayland_wrapper/qwldatadevicemanager.cpp)
serves compositor-owned offers synchronously. The adapter substitutes only that
manual offer's request table. Qt-generated application and drag-and-drop offers
keep their existing implementation. This depends on audited private Qt symbols
and is guarded to exactly 6.12.0; a different Qt build requires a new audit.

Each substituted resource holds its own MIME snapshot and a shared writer group.
The snapshot is captured when the first MIME-offer event is sent. Receiving an
older offer must return that offer's bytes, even after another copy. A manual
offer encountered only after advertisement has no attributable snapshot and
returns no bytes rather than the current clipboard. Resource destruction releases
its snapshot. No worker keeps a pointer to the compositor, QObject or Wayland
resource, and compositor teardown cancels the group.

`ClipboardTransfers` owns every submitted descriptor, including rejection and
thread-start failure. It sets nonblocking and close-on-exec flags before starting
work and admits at most eight active transfers per group. A writer sends chunks
up to 64 KiB with a 30-second idle and 120-second total deadline. Normal pipe
writes check cancellation at polling intervals of at most 100 ms. A rejected,
cancelled or timed-out transfer closes its descriptor; a timed-out transfer may
have delivered only part of the data. Threads are detached at creation, avoiding
a throwing thread constructor or detach operation through the C dispatcher.

SIGPIPE is blocked and consumed in the writer thread. The process-wide signal
disposition is unchanged. The group destructor cancels without waiting for a
client to drain its pipe; shared state and byte buffers outlive active jobs.

## Verification and limits

Worker tests cover exact byte delivery, a deliberately non-reading 4-KiB pipe,
admission rejection, idle and total deadlines, cancellation, empty data and an
injected thread-start failure. The early-close case closes the owned reader
between poll and write, forcing the write to raise SIGPIPE with the process's
default disposition. Isolated negative controls remove the signal mask or
nonblocking flag; the former dies by SIGPIPE, and the latter fails to retire
its two stalled transfers. The original large-pipe negative control passed and
was insufficient: its first chunk fit the pipe. It remains recorded separately.

Native tests use `tests/paste_probe.c`, a raw Wayland window that owns every pipe it
pastes into. The test spawns it with `posix_spawn` and owns it through a pidfd; no PID is
ever signalled after it could have been reaped. The tests are:

- `retainedClipboardOffersKeepTheirOriginalData`: the probe keeps its offer for A, the
  clipboard becomes B, then it reads A from the old offer and B from the new one. An earlier
  in-process peer without a window lost keyboard focus after the first copy, and with it the
  second offer, so it never passed after its final hardening. Against the standalone fix
  (one global copy) the probe's old offer A pasted B.
- `aStalledPasteNeverBlocksTheDesktop`: a 4 MiB paste the probe never reads, then dropped
  mid-transfer. While the probe is still stalled, the compositor's thread must keep running
  (a watcher thread measures its heartbeat) and a key must reach the probe. If the thread
  stops for 2 s, the watcher tells the probe to drain its pipes, so an unfixed compositor
  fails the test instead of hanging it: before the fix it stopped for 2005 ms.
- `aPasteClosedAtOnceNeverKillsTheDesktop`: 20 readers close before anything is written.
  Before the fix the desktop died of SIGPIPE.
- `manyStalledPastesUseBoundedResources`: 64 stalled at once. The heartbeat and input must
  hold, there must be at most 12 more threads than before, all writers must let go once the
  readers do, and a fresh paste must then get all 4 MiB. Before the fix the compositor
  stopped for 2409 ms; with the standalone fix's unbounded writers it had 96 threads (31
  before).

The paste tests clear the test process's own clipboard before each scene. Otherwise an
earlier test's shell copy is offered first, and a test can read the wrong clipboard. A
desktop session starts with an empty one. Native tests run under a private
compositor/bus and do not inspect the user's clipboard.

The limits above bound writer admission, not Qt's underlying retention cache.
Qt still eagerly reads every offered MIME format without a transfer byte,
time or format-count budget. Preview limits do not bound that cache. MIME
snapshot bookkeeping and image encoding occur in the compositor thread.
Nonblocking mode does not guarantee a deadline on arbitrary device or regular
file descriptors, or against a peer changing a shared open-file description's
flags. This is not a security isolation boundary for hostile applications.
Replacing the retained reader with a bounded asynchronous service is separate
work. Leak detection is disabled in ASan runs; external system libraries and
browsers are not instrumented.
