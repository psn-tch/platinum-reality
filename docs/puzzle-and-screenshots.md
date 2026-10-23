# Puzzle and screenshots

**Apple → Puzzle** opens the native fifteen-tile game requested during hands-on
testing. Click a tile beside the empty square to slide it. Arrow keys move the
space; **Shuffle** starts another solvable board. The move counter and completion
message are part of this implementation. The numbered game uses original code
and the shared Platinum controls, without original Apple executable or artwork
files. It recalls the classic Puzzle desk accessory; exact IIsi-era geometry and
version-specific rules have not been historically authenticated.

**Alt+Shift+S** captures the Platinum Reality desktop and opens Screenshot. The
snapshot is taken before the tool opens. Drag on its preview to crop, or choose
**Entire Desktop**. Enter a PNG filename and choose **Save**; **Cancel** discards
the memory-only capture without saving. The suggested filename is under
`~/Pictures/Screenshots`, using the configured Pictures location where available.
An existing file is never replaced. Newly saved screenshot files are owner-only.

The tool captures this compositor's visible desktop at its current pixel size;
it does not capture other sessions, the outer backend's lock screen, audio,
cursor imagery, or hidden application content. Capture is limited to 64
megapixels. There is no screen-recording, upload or clipboard integration in this
slice. Selections are expressed in capture pixels, including at fractional
display scales. The image provider retains the capture only until the dialog
closes or the desktop process ends.
