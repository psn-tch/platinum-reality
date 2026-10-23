# Clipboard integration review — 2026-10-21

## Release blocker discovered after the ordinary workflow gate

The 71-case normal/sanitized source gates and installed/session checks passed,
but they did not exercise an uncooperative reader of the compositor-owned
clipboard. Sammy subsequently reproduced a main-thread stall in Qt's synchronous
`DataDeviceManager::comp_receive` writer and identified the early-close SIGPIPE
path. The current batch is explicitly blocked from release. Its passing receipts
remain evidence for their tested workflows, not proof of safe clipboard serving.

The standalone safety patch is reviewed separately from the optional stack.
Its detached writer needs nonblocking writes, bounded worker ownership and safe
thread-start failure handling. The regression must hold the reader blocked
deterministically and avoid signaling a PID that may have exited and been reused.
None of these safety follow-ups is claimed complete in this checkpoint.

**Update 2026-10-22 (Sammy):** all three are now done and tested; see
[clipboard-transfer-safety.md](clipboard-transfer-safety.md) and the status entry for the
safety candidate. The blocker stands for this original candidate, which is never selected.

Sammy's batch-1 clipboard and Command-key changes were reviewed separately from
his later attention and primary-selection batches. The integration branch is
`work/clipboard-20261021`, based on `f236012`. No release selection is implied by
this document.

The original 19-file patch has SHA-256
`e5db9a9f550551544fae26a934c57885b59977bdb3917bf5b68c64c6c8165712`.
All qualified file hashes were checked against the private review copy, and the
changed runtime hashes were checked against both its normal and sanitized
pre-run manifests. Those runs reported 68 passing native checks each. Their
original failed Firefox context-menu run remains unexplained and preserved;
subsequent passes do not establish a causal fix.

Sammy corrected the original trace interpretation during follow-up review:
QtTest's 2000-message warning cap had truncated the trace, so absence of later
pointer events was not evidence that no events occurred. A later failed run
reported the click inside the input region, contradicting the excluded-region
hypothesis. A Firefox main-thread futex wait is an observation, not a symbolized
causal diagnosis. Future native gates use `-maxwarnings 0` to retain complete
protocol diagnostics. The browser context-menu failure remains unresolved.

## Independent regression: held Command across a focus change

The added `commandChordDoesNotConsumeTypingAfterFocusChanges` test holds
Command-C in one disposable application, switches to another before releasing
the chord, returns, and types `ccc`. Against the original patch it received
`cc`: the old window consumed the first plain C press using stale chord state.
The original failing run is
`build/artifacts/native-runs/20261021T182509-1782250`, 2 passed / 1 failed,
19005 ms, cleanup status 0.

The integration delta retires translated keys when their item loses focus and
forgets the item's physical-to-translated map. It sends releases and restores
physical modifiers only if that surface still owns the seat's keyboard focus;
it never focuses a different application to send cleanup events. Validation is
recorded separately from the original patch's qualification.

## Independent regression: deferred clipboard ordering

The second added test focuses a disposable raw Wayland client without a data
device, defers a shell copy, and has an owned Qt client make a newer copy.
Returning focus and pasting produced the older shell copy against the original
clipboard adapter. Run `build/artifacts/native-runs/20261021T183221-1798933`
records 3 passed / 1 failed, 19588 ms, cleanup 0; the held-key regression passed
in that run. The fix discards deferred shell data on an application selection
request, including explicit clear and repeat requests.

The image preview now checks its declared source dimensions before decoding,
with a 16-million-pixel budget as well as a 32-MiB encoded budget. Display is
limited to 2048 pixels per axis without enlarging small images. A third test
checks small-image geometry, a highly compressible image exceeding the source
pixel budget, the text-preview budget, and preservation of original MIME bytes.
It does not certify every decoder or rich-format application's paste behavior.

## Fixture readiness investigation

The first independent focused sanitizer run passed eight cases and failed
`shellMenusCloseOnAKeyLikeMacOS9`: after closing the menu, its owned probe
reported empty text rather than `ok`. Cleanup passed. The unchanged test passed
alone, after the image case, and in the original focused sequence. These passes
do not explain the original failure.

Further diagnostics reproduced a different all-text-empty failure in the held-key
test. In two of eight controlled trials, the external probe reported both its
window inactive and its text widget unfocused before typing; neither received
widget key presses. The compositor had already assigned focus. The probe became
active later. That differs from the stale-map defect, which drops just the
first C and yields `cc`.

The test fixture now reports input readiness over its private bus. These tests
wait for its active window and focused text widget before ordinary typing,
rather than using compositor focus alone. A helper also waits for the QML client
delegate before requesting activation, instead of capturing a null item once.
Production input timing and all existing text assertions remain unchanged.
The original menu failure did not record client readiness, so that run's exact
cause remains unproven. Original logs, unchanged controls, diagnostic failures,
and isolated negative controls remain separate evidence.

The completed isolated negative control disables only focus-out chord retirement
in a private compiled object. With the corrected readiness test, it still fails
exactly `cc` versus `ccc`: run `20261021T185513-1808413`, 2 passed / 1 failed,
18914 ms, cleanup 0. Canonical source, ELF, object and Ninja/linker metadata
hashes are protected during that build. Earlier control setups lacked the drop
receiver, opened a modal shell error, and could not test focus. Supplying that
helper with the unchanged second control executable produced the expected
regression. Those setup failures are not counted as evidence for the product fix.

## Dependencies and limits

The request-table adapter depends on the exact private Qt 6.12.0 symbols and
dispatch behavior. The runtime version guard does not promise source or binary
compatibility with later Qt releases. No system Qt library is patched.

Independent source review used Qt's tagged
[data-device implementation](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/wayland_wrapper/qwldatadevice.cpp),
[retained-selection implementation](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/wayland_wrapper/qwldatadevicemanager.cpp),
and [keyboard implementation](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/compositor_api/qwaylandkeyboard.cpp).
The original clipboard source is retained; no upstream implementation file was
copied into the product.

Qt's retained reader eagerly caches every offered format. Filtering GTK's
selection-protocol targets fixes the demonstrated stall, but does not put a
byte, time, or format-count budget on arbitrary clipboard transfers. A source
that never finishes a format can leave the retained preview stale. Image/text
preview limits are not transfer limits or a security boundary. A future bounded
retention service requires separate design and deliberately uncooperative
owned-client tests.

Historical observations attributed to the Mac OS 9.2.2 VM in the batch packet
are Sammy's reported observations, not a new independently authenticated
historical corpus. Rocky did not boot a VM during this integration review.
Real browsers, Konsole, and X11 fixtures provide compatibility evidence for
those tested paths, not universal application or game compatibility.
