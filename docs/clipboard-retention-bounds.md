# Bounded automatic clipboard retention

**Source candidate, not runtime qualified.** Normal and ASan/UBSan builds and
native protocol tests must pass before integration. It stacks on explicit NULL
tool clearing. No production session or release has been changed.

The adapter remains restricted to **Qt 6.12.0**. The exact upstream
[retained reader](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/wayland_wrapper/qwldatadevicemanager.cpp)
has no byte budget and keeps obsolete read notifiers to avoid sending a client
SIGPIPE. Retention must therefore be bounded *before* collecting the payload,
not only when drawing its preview. Qt's eager reader is disabled. The adapter
fills its retained cache with bounded results, leaving the live application
source installed. The private
[compositor notification](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/compositor_api/qwaylandcompositor.cpp)
also checks the eager-reader flag, so completed captures and shell overrides
notify the adapter's snapshot/preview path directly. Snapshotting precedes offer
advertisement.

## Limits and deliberate compromises

| Resource | Limit |
| --- | --- |
| Automatic ordinary-app capture | 64 MiB aggregate, 64 distinct MIME types, 10 seconds |
| MIME name accepted for retention | 4096 UTF-8 bytes, no NUL |
| Reader work per event-loop activation | 1 MiB |
| Capture or obsolete drain pipes | One total, not one per copy |
| Immutable offer snapshot history | 32 copies and 64 MiB aggregate |
| Compositor-owned paste writers | Eight active and 64 MiB aggregate payload |
| Writer deadlines | 30 seconds idle, 120 seconds total |
| Tool capture | Existing 64 MiB / 10-second cap, now 1 MiB per activation |

Text and PNG are preferred. Completed MIME payloads fitting the capture budget
are retained; the incomplete oversized or timed-out MIME is discarded in full.
The live source still serves its own clipboard, so an application can paste data
larger than the retention budget while the copying application remains alive.
Drag-and-drop offers keep Qt's original path. Shell/tool snapshot copies use the
same byte/format caps; an unsupported empty shell snapshot clears rather than
recovering the previous copy.

Cancelling or exceeding a limit changes the *one* active read pipe into a
discard-only drain, with no retained byte buffer. It stays open until EOF to
avoid killing a producer through SIGPIPE. Further selections do not allocate
more drain pipes: when it is free, only the latest still-live source is captured.
A producer holding that pipe forever prevents later *automatic retention*, but
does not prevent live app-to-app paste, tool copy, shell copy, input or file
operations. This is a resource-bound tradeoff, not a promise to time out the
producer's write end. Teardown closes the owned pipe with the compositor.

New user intents invalidate obsolete results. A repeated installation of the
same GTK source preserves its already running capture, while still superseding
older tool/shell requests. Source destruction alone is not a new copy intent;
the already requested MIME may finish, but no additional MIME is requested from
the dead source. Before capturing a genuinely new ordinary source, stale cached
bytes are forgotten so a failed new capture cannot revive an unrelated old copy.

Old offers weakly reference snapshots. Small recent offers preserve their
original data across a new copy. Count/byte eviction or an explicit clear makes
an expired offer return EOF; it never serves a newer copy. An already admitted
writer owns its payload until completion/cancellation and is accounted by the
separate writer byte budget. Byte admission is released on all failure paths.

These are payload and service-resource limits, **not an RSS guarantee or a
security boundary against hostile Wayland clients**. QByteArray allocation
capacity, Qt's MIME metadata/cache, preview images and C++ bookkeeping add
overhead. Client-created Wayland objects and their metadata remain governed by
the wider compositor. The outer clipboard is still read/cleared only when this
process owns it. Previously pasted bytes cannot be securely recalled.

## Required qualification

- Reader unit tests: preserved binary bytes, format limits, oversized/stalled
  MIME, source exit, cancellation without SIGPIPE and one pipe across copy churn.
- Writer unit tests: independent count/byte budgets and byte reservation released
  after completion and injected thread-start failure.
- Native cases: existing GTK, tool ordering and owner-exit regressions; explicit
  NULL clears; ordinary raw producer holds its old FD through 16 newer sources
  with default SIGPIPE disposition and still receives keyboard input; original
  old-offer semantics within budget, count/byte eviction; a 65-MiB live paste
  succeeds but its oversized MIME is absent after owner exit.
- Full native normal and ASan/UBSan suites on the exact candidate after the
  shared release-gate slot is returned. None of these new checks is reported as
  executed yet.
