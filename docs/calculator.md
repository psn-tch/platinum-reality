# Calculator desk accessory

Review packet `rocky/calculator-nov3`, based on update32 `6d7b1b3`. Apple menu ▸
Calculator opens one desktop-owned, modeless window. Its original Platinum frame
uses `ShellWindowFocus`, supports title-bar dragging and Command-W/close-box
closing, and restores the application or desktop window it was opened from when
appropriate. Closing preserves the calculation for this login; nothing is saved
to disk. The README, first-session guide and Help Center describe its controls.

The keypad and keyboard share a small explicit state machine for four-function
arithmetic, decimal input, sign changes (kept in the engine; 9.2.2 has no key for it), percentages, backspace, clear and repeated
equals. Arithmetic happens in entry order. Addition/subtraction percentages use
the accumulator as their base (`200 + 10 % = 220`); multiplication/division use
the percentage as a fractional operand. Division by zero and numeric overflow
show Error; C, Escape or a new digit recovers. Entry is bounded to 15 digits and
results use ordinary floating-point arithmetic with a 15-significant-digit display.
No input is passed to an expression evaluator, process or shell.

Edit ▸ Copy and Command-C emit the current valid displayed number through the
existing shell clipboard bridge. The focused calculator claims desktop file
shortcuts so typing does not rename, duplicate or trash a selection behind it.
Application cycling and global screenshot shortcuts retain their existing route.
Escape clears this accessory; it does not close it.

## Validation

Seventeen arithmetic fixtures cover chaining, replaced operators, repeated equals,
decimal rounding, percentages, sign, backspace, implied operands, input bounds,
ignored text, zero division and recovery, and overflow. The desktop workflow
checks mouse/keyboard input, copy signals, shortcut ownership over a selected
fixture document, one instance, clear/close and return to Control Panels. A
private offscreen render is visually inspected; it is an original drawing and
is not a certified historical pixel match.

Focused normal and ASan/UBSan runs each pass all 20 reported cases (18 checks
plus setup/cleanup; leak detection off). The full desktop run has 73 passes and
three failures, all existing supervisor socket listeners denied by this executor.
The first window test exposed inherited Escape closing the accessory; the frame
now lets an accessory choose Escape's behavior, with a single-close signal check.
The later keyboard-after-copy focus concern did not reproduce and needed no
focus-policy change. Initial results and the final checks remain in private logs.

A native workflow compiles for the actual Apple-menu item, arithmetic through
the host keyboard, copying 36, closing and pasting the result into the original
application. Execution remains pending: this executor denies the compositor's
socket venue. Offscreen copy-signal tests do not prove native clipboard transfer.
No integration, native gate, release selection or session change is included.

The shared DesktopWindow frame is also included by the independent About This
Computer packet. Keep one `desktop-window-assets` resource when combining the
packets; the calculator adds the configurable Escape behavior for accessories.

**Layout (Sammy, 2026-11-04):** after the 9.2.2 reference VM's Calculator: a black rounded frame
and title, a white display, keys C = / *, 7 8 9 -, 4 5 6 +, 1 2 3 and a tall =, 0 (wide) and .;
copying is Edit ▸ Copy (Command-C) and Edit ▸ Paste types a copied number in.
