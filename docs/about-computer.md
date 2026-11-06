# About This Computer window

Review branch `rocky/about-computer-nov3`, based on update32 `6d7b1b3`.
The Apple menu's first item opens one desktop-owned, modeless window. It uses
an original Platinum frame and `ShellWindowFocus`, supports dragging, close
box, Command-W and Escape, and returns focus to the application or desktop
window it was opened from when that return target is still appropriate.

The window shows the actual Linux distribution/kernel version, usable RAM,
swap total/usage, available RAM and an application list with memory bars. It
samples once a second while open, stops when closed and refreshes on reopening.
It uses no imported Apple drawings and does not identify Linux as Mac OS 9.

**Layout (Sammy, 2026-11-04, from the 9.2.2 reference VM):** a white banner with the system's
name in large original type, a grey strip of facts in two columns (Version, Built-in Memory,
Virtual Memory with the swap in use, Largest Unused Block with the available estimate beside it;
the kernel version at the right), then the running applications, each with its icon, resident
size and a 9.2.2-style bar.

## What the numbers mean

Measurements use bounded, read-only `/proc/meminfo` and `/proc/PID/status`
reads, following the [Linux kernel proc documentation](https://docs.kernel.org/filesystems/proc.html).
`MemAvailable` estimates memory available for new work without swapping; it is
not a largest contiguous application heap. The classic "Largest unused block"
row therefore says "Not reported" and the available-memory estimate has its
own label. Virtual memory is explicitly labelled as swap here.

Each running application's known native Wayland process owners contribute
their approximate resident set (`VmRSS`). Duplicate windows from one process
do not count its RSS twice. The desktop includes its own process. Helper and
browser content-process trees are not inferred; shared pages may be counted
in multiple applications. Bars compare the known resident measurements and
are not a partition of total physical memory. An X11 bridge is not the X11
application: its RSS is never substituted for that program's measurement.
Unknown, exited, unreadable, oversized or malformed samples stay unavailable.
Nothing reads process arguments, environments, memory contents or credentials.

## Checks and limits

The component fixtures cover RAM/swap units, overflow/negative/duplicate and
oversized fields, missing/exited processes, duplicate windows, refreshed RSS
and unavailable measurements. The rendered window test checks one instance,
modeless opening, live refresh, stopped sampling on close, Command-W/Escape
and reopening, including dragging the title bar. A private offscreen render
has been visually inspected. The focused normal and ASan/UBSan runs each pass
all five reported cases (three checks plus setup/cleanup; leak detection off).
The full desktop run reports 57 passed and three failed: all three failures
are existing supervisor socket listeners denied by this executor. This is
not a full-suite green result.

The native workflow opens the actual Apple-menu item from a real application,
checks its resident-memory row, closes with the owned host's virtual keyboard
and verifies typing returns to the application. It compiles but remains pending
in this executor: its socket/DRM venue cannot start. No native visual accuracy,
multi-monitor/hotplug or installed-session acceptance is claimed by component
rendering. Canonical sources, selected release and the user's session stay
unchanged; this is a review packet for later integration/gating.
