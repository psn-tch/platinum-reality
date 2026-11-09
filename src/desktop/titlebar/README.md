# Title-bar boxes

The close, zoom and WindowShade boxes (normal, pressed and inactive), 13 x 13, are the
PlatinumWayland KDE Aurorae theme's drawings (`assets/aurorae/themes/PlatinumWayland/{close,maximize,shade}.svg`,
elements `active-center`, `pressed-center`, `inactive-center`), exported one state per file with
`rsvg-convert -i <element> -f svg`. PlatinumWayland is Lily Hewitt's own project, MIT licensed
(`LICENSE-PlatinumWayland`). They are original drawings in the Mac OS 9.2.2 Platinum style, not Apple artwork.

The exported percentage gray fills are normalized to exact hexadecimal values so Qt SVG
preserves the intended 8-bit palette (for example, `#888888` instead of truncating
`53.333333%` to `#878787`). The active zoom indicator uses an upper-left corner
outline, measured against the local reference. See `docs/fidelity/window-frame.md`
for geometry and the scope of the regional pixel comparisons. Pressed gradients and the zoom indicator are also measured; regional comparisons
exclude the reference cursor and match all remaining pixels of each control.
