# Ordering asynchronous clipboard copies

A data-control tool could submit a copy, leave its send pipe open, and finish
reading only after an application or shell made a newer copy. The old tool's
bytes then replaced the newer clipboard. Repeated tools also left superseded
reader pipes open until timeout. A separate case resurrected an older deferred
shell copy when a newer tool abandoned its copy.

The compositor now assigns an intent token when a tool submits its copy,
before reading its bytes. Application requests and shell-owned copies reserve
newer intents too. A tool can commit only its still-current intent; a newer
intent closes obsolete reader pipes immediately. Every new intent discards
older deferred shell data. Source destruction changes the selection generation
but does not itself constitute a new user copy, so closing the old application
cannot accidentally discard a newer tool copy.

This extends the existing Qt 6.12.0 compatibility adapter. It does not broaden
its supported Qt versions. The resource/selection logger, retention and
application offer behavior otherwise keep their existing roles.

## Deterministic evidence

`tests/data_control_probe.c` is a test-only raw ext-data-control client. It
holds each send FD until the test explicitly writes/closes it, so ordering does
not depend on a conveniently slow process or an arbitrary sleep. Tests cover
newer application, shell and tool copies; abandonment after deferred shell
copy; old source destruction; and cancellation/EPIPE for superseded readers.
All bytes and clients belong to the private test scene; no live clipboard is
accessed. The helper is not installed in the desktop payload.

Development evidence in the author's clone:

- `build/copy-order-negative-20261029`, native `20261029T065142-115449`:
  original implementation fails both new regressions (2 pass, 2 fail including
  Qt init/cleanup).
- `build/copy-order-positive-20261029`, native `20261029T065341-116440`:
  first implementation passes the initial cases (4 pass).
- `build/copy-order-deferred-negative-20261029`, native
  `20261029T065539-117375`: expanded deferred-shell case fails (3 pass, 1 fail),
  motivating the unconditional pending reset on every new intent.
- `build/copy-order-positive2-20261029/NOT-RUN.txt`: no test/build ran because
  Sammy held the shared heavy-work lease. This is not a passing result.
- `build/copy-order-final-20261029`: final source passes all 13 selected
  clipboard scenarios in the normal build (15 pass including init/cleanup),
  native `20261029T080822-213729`. Private runtime removed safely.
- Initial sanitizer launch stopped before tests at the external-style-plugin
  guard. Correcting the fixture path led to
  `build/copy-order-sanitized2-20261029`: 14 pass, 1 fail. The failure was the
  existing X11 middle-click case in a build configured without the bridge;
  both new regressions passed. This run is retained and is not a full pass.
- Final sanitizer run: **15 pass, 0 fail, 0 skipped** under
  `build/copy-order-sanitized3-20261029`, native
  `20261029T081809-217970`, configured with the same provenance-checked bridge
  as the normal build. Both the middle-click X11 case and all new regressions
  pass; private runtime removed safely. No production source changed to
  correct these fixture inputs.

The tests use private HOME/XDG, a dead system-bus endpoint, owned headless
Wayland and a private session bus, with host Handheld and audio access disabled.
The sanitizer build instruments the project's C++ with ASan/UBSan, with leak
checking disabled; system Qt/GTK, external Xwayland and the Rust bridge are not
instrumented. This is focused regression evidence, not a complete release gate
or a claim of hostile-application isolation. The integrator must run the
current full release gates on the combined source.

## Remaining work

An explicit NULL selection from data-control tools is still ignored by the
existing adapter. Ordinary application clipboard retention through Qt still
has different, incompletely bounded memory behavior. This patch does not claim
to solve either issue, change primary-selection semantics, or define a new
clipboard permission policy.
