# Menus stay on screen (xdg-positioner constraint adjustment)

Found 2026-10-20 (Claude) while testing menus on a second monitor. It affects a single screen
just as much.

**Defect.** Wayland applications cannot see screen positions. They describe a popup (a context
menu, a submenu, a tooltip) with an `xdg_positioner`: an anchor, a gravity, an offset, and which
adjustments are allowed if it would not fit (flip, slide, resize). The compositor must apply
them. Qt 6.12's compositor does not: its popup integration places every popup at the
unconstrained position (its source says "TODO check positioner constraints etc... sliding,
flipping"). A menu opened near the right or bottom edge was therefore drawn off screen. In
`NativeTests::popupMenusStayOnScreen` a Qt menu opened at the bottom-right corner sat at
(1104, 753), 141×66, on a 1120×760 desktop.

**Fix.**

- `src/desktop/popup_placement.*`: `placePopup()` is a pure implementation of the protocol's
  rules. Per axis, if the popup crosses the bounds: flip the anchor and gravity if the
  flipped popup fits; else slide it inside (keeping its start visible when it is larger); else
  resize it to the visible part. The original offset is kept, as xdg-shell specifies.
  Each step happens only if the client allowed it, so
  xwayland-satellite's unadjustable popups are kept as requested.
- `ClientSurfaceItem::constrainPopup()` (via `reconcileChildren`, including submenus):
  - The bounds are the monitor under the popup's anchor, from `manager.displayOutputs` passed in
    as the item's `screens`. If no monitor contains the anchor, the monitor overlapping the
    parent window most is used; failing that, the whole window.
  - The bounds are converted to the parent's window coordinates, rounded inward.
  - The corrected geometry is sent with `QWaylandXdgPopup::sendConfigure`.
  - On every `configuredGeometryChanged` the item is moved to the configured position, after
    Qt's integration has reset it to the unconstrained one.
  - **Exact popup item geometry.** Qt sizes a popup item from its rounded implicit size, so at
    a fractional host scale (1.5) a 141-pixel menu was drawn 142 wide and about 1.6 pixels off.
    Each popup item is now sized in its parent's units, as `ClientSurface` sizes windows, and
    positioned through the parent's `mapFromSurface`. That also makes clicks inside menus map
    exactly.
- Tests:
  - `popup-placement-tests` (local suite): the corner case, slide when a flip does not fit,
    allowed-only adjustments, a menu taller than the screen, the flipped submenu offset, and a
    second monitor's bounds. Rocky's 2026-10-21 review found that the first version
    inverted the offset on a flip. Two protocol-derived regressions failed before
    the correction (x=844 rather than836, and an unnecessary slide to50 rather than
    a valid flip to31). They require the original offset on both axes. The first
    new expectation used30, missing the 1-pixel anchor's far edge; the first local
    gate caught that expectation error and its failure is retained separately.
  - `NativeTests::popupMenusStayOnScreen` (default native suite) with a real Qt client and a
    real host click: the menu at the corner now sits at (978, 693), fully visible. The
    screenshot was checked.
  - `NativeTests::popupMenusStayOnTheirMonitor` (two-screen): a menu at monitor two's corner
    stays inside monitor two (shorter than the desktop), and one at monitor one's right edge
    stays on monitor one.
