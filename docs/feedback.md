# Hands-on feedback queue

User feedback during the 2026-10-18 standalone session takes priority over the
earlier research-only scope. Work remains in this independent native project;
the protected KDE projects and current live session are not modified by builds.

| Request | State |
| --- | --- |
| Firefox text input | Installed and observed running after login; explicit daily-use typing confirmation pending. Actual native typing/reacquisition regression passes. |
| Kitty does not open | Explicitly deprioritized; user prefers Konsole. Private experiment stopped, no system changes. |
| Alt+D opens Applications | Selected for next login; complete normal/sanitized native gates pass. |
| Alt+Q quits the active application | Implemented using normal toplevel close requests; native close-veto, multi-window grouping and Editor Save/Cancel checks cover it. |
| Wallpaper chooser | Implemented with preview, saved placement and cancellation; native and local checks pass. |
| Classic sliding-tile Puzzle in Apple menu | Selected for next login; legal tile play/launch/close passes. Shuffle-focused arrow routing fixed and regression-tested. |
| Alt+Shift+S opens screenshot tool | Selected for next login: crop/save/cancel passes in the full normal/sanitized gates. |
| System 7-style quick-controls widget | Selected for next login: volume/mute, active-display brightness, IWD/NM Wi-Fi, BlueZ. Stateful fake-device tests and read-only actual status pass. Hardware mutations await actual user use. |
| Browser photo/download saving fails | Popup-focus fix selected for next login. Real right-click Save Image As, HTTP/Blob/attachment bytes pass in Firefox/Chromium; Chromium Save File picker too. Actual ChatGPT account/profile confirmation remains open, as do intermittent earlier Firefox stalls and Chromium chooser Return. See browser-downloads.md. |
| Existing emulators, especially tuned Ymir | In progress. Known Ymir wrapper forces X11. Native Wayland/software UI launch passes; exact-wrapper adapter and XDG preservation selected. Separate Radeon GPU probe and Ymir OpenGL startup pass; inner welcome placement and full settings/gameplay/audio/controllers remain open. See emulators.md. Other emulators need individual review. |

Updates are staged as distinct user-local releases. The running desktop is not
restarted or overwritten automatically. An update becomes active at the next
normal logout/login. GitHub work has been put aside at the user's request.
