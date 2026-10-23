# Status items and notifications

Status: **implemented (Claude, 2026-10-20)** with the defaults below. psn_tch can still choose a
different form; see "Open questions".

## As built

- `src/desktop/status_items.*`: `org.kde.StatusNotifierWatcher` at `/StatusNotifierWatcher` on the
  session bus. Each item is keyed by its unique bus name plus object path.
  - Items registered by object path (libappindicator) or by bus name (KDE) are both accepted, at
    most 32. They are removed when their owner leaves the bus.
  - Properties are read with `GetAll` and refetched on `New*` signals (coalesced over 30 ms).
  - Icons come from `IconName` and `IconThemePath`, falling back to `IconPixmap` (ARGB32, network
    byte order, nearest 40 px). In `NeedsAttention` the attention icon is used.
  - Menus are decoded by `MenuBroker::decodeLayout`, refreshed on `LayoutUpdated` and
    `ItemsPropertiesUpdated`, prepared with `AboutToShow`, and invoked with `Event(id, "clicked")`
    after checking that the entry exists and is enabled.
  - Clicks (`Event`, and `Activate` for "Open *Title*") are sent no-reply-expected, as are the
    application menu's (`MenuBroker::invoke`). Nothing waits for their answer. A call that awaits
    an answer holds a slot on the bus until the application answers or quits, so a hung
    application's menus used to use them all up.
- `scripts/session_support.py`: the private session bus keeps the installed session bus's limits
  (`/usr/share/dbus-1/session.conf`, e.g. 50000 pending replies per connection), except the 16 MiB
  message size. Without them dbus-daemon's built-in defaults applied, including 128 pending
  replies. On 2026-10-21 the live desktop logged "maximum number of pending replies per
  connection has been reached" 82 times, and its later calls (menus, Bluetooth, the wallet)
  failed. As on the stock bus, there is no reply timeout: some calls rightly wait for the user.
- `src/desktop/notifications.*`: `org.freedesktop.Notifications`.
  - Capabilities: actions, body, icon-static, persistence. Markup is not advertised.
  - Images from `image-data`, `image-path`, `app_icon` or `icon_data`, in the specification's
    order, at most 1024², shown at 64 px.
  - Timeout −1 means 6 s, but critical urgency stays until dismissed. Timeout 0 never expires.
  - At most four alerts; the oldest closes as expired. Close reasons: 1 expired, 2 dismissed or
    action chosen, 3 closed by the sender.
- `src/desktop/icon_store.*`: images for QML at `image://platinum/<key>@<revision>`.
- QML:
  - `ControlStrip.extraModules`: tray modules after the built-ins, objectName
    `control-strip-status-<Id>`.
  - `Desktop.statusMenuEntries`: "Open *Title*" (`Activate`) unless `ItemIsMenu`, then the app's
    entries, with submenus flattened under a disabled heading.
  - `NotificationAlerts.qml`: Platinum utility windows at the top right, with a close box, the app
    name in the title bar, a body click for the default action, and buttons for the other actions.
- Tests (real client libraries):
  - `NativeTests::notificationsAppearAsPlatinumAlerts` uses libnotify's `notify-send` 0.8.8 to
    check icon, replacement, close box, expiry, action buttons, default action, CloseNotification
    and the four-alert cap.
  - `NativeTests::trayItemsBecomeControlStripModules` uses `tests/status_item_probe.py`
    (libappindicator3 through PyGObject) to check the module and icon, the menu with check and
    disabled entries, a clicked entry reaching the app, attention and passive states, and removal
    when the app quits from its own menu.
  - `NativeTests::clicksOnAHungApplicationsMenusNeverExhaustTheBus` uses
    `tests/silent_menu_probe.py` (dbus-python), an app whose tray item and application menu never
    answer clicks. 200 tray-menu clicks, 200 Activates and 200 application-menu clicks must all
    arrive marked no-reply, and the desktop's own calls must still work. Before the fix only 128
    arrived (85 + 43), none no-reply, with 472 "pending replies" errors.
  - `tests/session_support_tests.py` checks the private bus's limits against the installed
    `session.conf`. It also checks that 300 unanswered calls all time out at the caller
    (`NoReply`) rather than being refused (`LimitsExceeded`, as before the fix).

## Not yet done

- The Application-menu ◆ mark for apps that sent a notification.
- `SecondaryActivate`, scroll events and tooltips on tray modules.
- Alert sounds (F17).

---

Original design notes follow.

## Problem

The native session runs applications on its own private session bus (`dbus-run-session` in
`packaging/platinum-reality.in`). Nothing on that bus owns `org.kde.StatusNotifierWatcher` or
`org.freedesktop.Notifications`. As a result:

- Steam, Discord, blueman and other tray applications have nowhere to show their icon or menu.
  Some refuse to minimise to the tray, and some keep a window open instead.
- `notify-send`, browsers and chat apps get errors or silently drop notifications.

## Period mapping (PROP, needs psn_tch's agreement on the look)

Mac OS 9 has no tray or notification centre. The closest period equivalents:

- **Status items become Control Strip modules.** Each registered StatusNotifierItem appears as a
  module after the built-in ones. It uses the app's icon and the ▸ pop-up indicator.
  - Clicking opens a pop-up menu above the strip, built from the item's `com.canonical.dbusmenu`
    layout, with "Open *Title*" (`Activate`) first. That matches V922 E12's "first item opens the
    owner".
  - Items with `ItemIsMenu` open the menu directly.
  - `NeedsAttention` swaps to the attention icon. A `Passive` status hides the module.
- **Notifications become Notification Manager alerts.** Mac OS 9's Notification Manager
  (`NMInstall`) did two things: it marked the requesting application in the Application menu
  (the icon alternates with a ◆ mark), and it optionally showed a small alert.
  - A non-modal, Platinum-framed alert appears at the top right below the menu bar, showing the
    app icon, summary and body.
  - It is dismissed after the requested timeout (default 6 s) or by clicking its close box.
    Clicking the body invokes the "default" action.
  - Several alerts stack.
  - The Application menu marks the sender with ◆ until it is next activated.

## Implementation outline

- `src/desktop/status_items.*`: `org.kde.StatusNotifierWatcher` (and the `org.freedesktop.`
  alias some libraries probe) on the session bus.
  - Registration accepts a bus name or an object path (some clients pass just a path).
  - Unregistration on name loss is tracked with a `QDBusServiceWatcher`.
  - Each item's properties are read with `GetAll`, and its `New*` signals are followed.
  - Icons: `IconName` through the icon theme (plus `IconThemePath`), or `IconPixmap` (ARGB32,
    network byte order, the size closest to 20 px), served by a QML image provider.
  - Menus are decoded with `MenuBroker::decodeLayout` (already bounded and validated), with
    `AboutToShow` before showing, and `Event(id, "clicked")` to invoke.
- `src/desktop/notifications.*`: `org.freedesktop.Notifications`.
  - Methods: `Notify`, `CloseNotification`, `GetCapabilities` (`body`, `actions`,
    `icon-static`, `persistence`) and `GetServerInformation`.
  - Signals: `NotificationClosed` and `ActionInvoked`.
  - Body markup is shown as plain text. Image hints are limited in size, and the number of open
    alerts is capped.
- QML: the strip's module list gains the status items. A new `NotificationAlert.qml` stack is
  added.
- Tests:
  - a Qt probe that registers a StatusNotifierItem with a DBusMenu on the scene's private bus
    (registration, icon, menu, activation, unregistration on exit)
  - `notify-send`-shaped calls through `QDBusInterface` (show, replace, timeout, close, action)

## Open questions for psn_tch

- Whether status items should sit in the Control Strip (period form, as above) or in a
  separate area of the menu bar (modern form).
- Whether notifications should also play the Mac OS 9 alert sound (needs F17 interface sounds).
