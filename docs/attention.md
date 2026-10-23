# Applications asking for attention, and the bell

Source: `src/desktop/attention.*`, wired in `NativeManager`, with the Application menu in
`Desktop.qml`. These protocols aren't in Qt's compositor, so they are implemented directly
with libwayland, like the appmenu bridge.

## Mac OS 9.2.2 (checked in psn_tch's 9.2.2 VM)

- The Application menu marks an application that wants attention with **◆** (Notification
  Manager), next to the ✓ of the active one.
- While one wants attention, the Application menu's title alternates about once a second
  between the active application (icon and name) and the waiting application's small icon.
- Nothing comes forward on its own; the user brings it forward.

## xdg-activation-v1

An application asks for a token, then presents it with the surface it wants activated.
Firefox does this for a link opened from another application; Qt and GTK 4 do it for
`requestActivate()`. The usual focus-stealing prevention applies:

- A token is **granted** only if, when it was committed, the requesting application had the
  keyboard (and the surface it named, if any, is its own). The desktop can also grant itself
  one (`grantedToken()`, e.g. for applications it launches).
- A token works **once**, within **10 s**. The table keeps at most 256 tokens.
- `activate` with a granted token brings that window forward (as clicking it would). Anything
  else (unknown, reused, expired or not granted) **never takes the keyboard**. It marks the
  application as wanting attention instead: ◆ and the alternating title, until the application
  is next active, or quits.

## Notifications

Acting on an alert (a click on it, or one of its buttons) sends `ActivationToken` with a
desktop-granted token just before `ActionInvoked` (Desktop Notifications spec 1.2). The
application can then come forward with it, so replying to a message from its alert raises
the mail or chat window.

## xdg-system-bell-v1

`ring` plays the alert sound chosen in Sound ▸ Alert Sounds. At volume 0, or with
no player, the menu bar flashes, as for any alert (`docs/alert-sounds.md`). Qt's
`QApplication::beep()` and GTK 4 use this protocol; terminals ring it for BEL.

## Tests

`native-tests applicationsAskForAttentionAndRingTheBellLikeMacOS9` uses the Qt probe through
Qt's own protocol clients:

- An unfocused application asking is marked and doesn't take focus.
- The title alternates, and the menu shows ◆.
- Activating the application clears the mark.
- A desktop-granted token brings it forward once; the same token again only marks it.
- The bell gives an alert (a flash, since the fixture is silent).

`notificationsAppearAsPlatinumAlerts` (real `notify-send`) also checks that `ActivationToken`
arrives before `ActionInvoked`.

## Limits

X11 programs reach this through xwayland-satellite, which binds xdg-activation when offered.
Their urgency hints aren't mapped.
