# Pop-up folder windows

Lily Hewitt's request (2026-11-09). Reference: a disposable clone of the local Mac OS 9.2.2 VM at
640×480 (`build/artifacts/sammy-popup-20261109/`, private and gitignored). The original disk was
never opened. QEMU 11.1.2 crashed repeatedly in its own translated code (SIGSEGV/SIGILL at random
addresses, once while idle); the study ran from saved VM states (`savevm`/`loadvm` on the clone).

## Measured in 9.2.2

- **View ▸ as Window / as Pop-up Window** follow as Icons / as Buttons / as List, after a separator.
  They are disabled while no Finder window is active.
- Choosing **as Pop-up Window** blinks the item, then the window opens as a pop-up on the bottom of
  the screen at its own width, its body reaching the screen's bottom edge. The test window went to
  the screen's left edge.
- **Tab** (in place of the title bar): 20 px tall. A black top row, a white highlight row, a
  `#cccccc` field, a `#999999` row along the bottom (where the tab meets the body) and an inner
  right shadow column, then a black right edge and a `#444444` drop-shadow column beside it from
  the fourth row down. A 16 px folder icon at x5/y3; the name in the bold menu face from x24.
  The tab measured 116 px wide for "Apple Extras".
- **Grips**: diagonal stripes in both top corners of the pop-up's header. Each row has white every
  fourth pixel with `#888888`-range grey between, bounded by a `#555555` diagonal, 11 rows tall.
  The right grip is the mirror image.
- **A click on the tab closes the window**; the pressed tab shows for a frame, then the window is
  gone. The closed tab sits on the bottom edge of the screen, and the Control Strip covers it there:
  a screenshot of the strip area was pixel-identical before and after.
- **A click elsewhere** (on the desktop) closes an open pop-up.

## Not verified (implemented from Mac OS 8/9 behaviour as documented, not measured)

- Dragging a tab along the bottom, and dragging it up to make a regular window again: drags did not
  register in the slow, unstable emulator (they arrived as clicks).
- Dragging a title bar to the bottom of the screen to make a pop-up.
- A file held over a closed tab springing it open (1 s, the same delay as spring-loaded folders).
- The closed tab's own colours: the Control Strip covered it.
- Opening/closing sounds: none are played; none were heard or checked.

## Platinum Reality

- The desktop draws pop-ups (`PopUpTab.qml`, the frame in `Desktop.qml`, `NativeManager::setPopUp`
  and friends). Only the Finder's own windows can become pop-ups, as in 9.2.2.
- A pop-up is open exactly while it is the active window: opening activates it, a click on the tab
  hands activation to the desktop, and anything else becoming active closes it. A closed pop-up
  never keeps the keyboard.
- The Finder's View menu asks the desktop over the private `org.frankenstein.Desktop` bus
  (`SetPopUpWindow`, `IsPopUpWindow`), only for its own surfaces (checked by peer executable and
  surface owner).
- Our Finder draws its own toolbar and header, so the grips cannot share its header: pop-ups have
  a 13 px grip strip of their own between the tab and the content (black line, white highlight,
  `#dddddd`). This is a deliberate adaptation.
- Tests: `finderPopUpWindowsOpenFromTabsAtTheScreenBottom` (native, real Finder): open, tab
  position, close to the bottom edge with the desktop active, reopen, click-away, tab drag, grip
  resize standing on the bottom, drag up to a regular window, title bar dropped at the bottom.
