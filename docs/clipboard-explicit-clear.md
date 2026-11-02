# Explicit clipboard clearing

This packet is source preparation, pending its normal and ASan/UBSan native
qualification. The original clear-only packet kept Qt's automatic retention;
the stacked bounded-reader change replaces it. Neither packet is qualified yet.

An `ext_data_control_device_v1.set_selection(NULL)` request is a new user intent,
not an empty completed copy. It immediately cancels older tool reads, invalidates
deferred shell copies, withdraws the live application selection, and forgets both
the compositor's snapshot and Qt's retained data. An application-issued NULL
selection also forgets Qt's retained bytes. Clipboard preview and the shell's own
clipboard are cleared; the outer desktop's clipboard is touched only if this
process owns it. A later genuine copy remains usable.

The integration remains pinned to **Qt 6.12.0**. These additional private-member
pointers use the existing explicit-instantiation adapter pattern, with no guessed
offsets. The audit references are the exact upstream tag:

- [DataDevice::data_device_set_selection](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/wayland_wrapper/qwldatadevice.cpp)
  cancels the old source, updates the manager, and sends a NULL selection to the
  focused client's data device when present. Its Resource argument is unused.
- [DataDeviceManager](https://github.com/qt/qtwayland/blob/v6.12.0/src/compositor/wayland_wrapper/qwldatadevicemanager.cpp)
  treats `overrideSelection(empty)` as a no-op. Setting a NULL source stops its
  active retention read but leaves already retained MIME data, which must also
  be cleared to prevent a later focus restoring it. Obsolete retention reads
  drain rather than updating that cache.

Read-only symbol inspection found the manager's nonvirtual selection methods
are not exported by the installed Qt library. Clearing therefore dispatches the
real virtual handler for each existing seat data device; it does not call those
non-exported methods. With no device yet, no ordinary source was installed, and
only the compositor-owned flag and caches need clearing. These additional private
member pointers are included in the same exact-version guard. Linking and native
behavior still require the pending VM qualification.

Tests use a private compositor and owned protocol clients. They cover NULL clear
without any Qt data device, unfinished tool cancellation and stale completion,
withdrawal of a live application's selection, retained data after its owner exits,
new focus, and a subsequent working copy. They do not use the user's clipboard.

Clear is not a secure-erasure operation: a recipient may already possess the old
bytes. The stacked bounds packet expires old offers on an explicit clear or
history-budget eviction; already delivered data cannot be recalled.
