# Bluetooth

## Applications launcher entry

Open **Applications** (Alt+D), search for **Bluetooth**, and press Return or
double-click the result. It opens the same panel and uses the same Bluetooth
service as the Control Strip. No second agent or external Bluetooth utility is
started. The entry also appears in recent applications after use.

Bluetooth is a reserved built-in launcher entry. It is available without an
adapter so the panel can explain an unavailable device, but it is never offered
as an Open With handler. An application entry on disk cannot replace its action.
The launcher releases its modal input before the panel opens; closing the panel
returns input to the previously active application. It passed the full normal
and sanitizer gates and is selected in `0.1.0-bluetooth-launcher-20261021T112439Z`
for the next login. See the [current handoff](../HANDOFF.md) and
[release evidence](evidence/bluetooth-launcher-update-20261021/summary.json).

Initial Bluetooth release: `0.1.0-bluetooth-20261021T061204Z`, qualified for the next user-chosen
logout/login. All software gates passed; real-device pairing and reconnection
still need a hardware trial. See the [release summary](evidence/bluetooth-update-20261021/summary.json).

psn_tch asked (2026-10-21) for Bluetooth in the native session that works like KDE, with all the
devices they had paired there. Pairings live in BlueZ (`/var/lib/bluetooth`), not in KDE, so every
device paired under KDE is already available. KDE's Bluetooth applet (bluedevil) only showed them
and answered BlueZ's pairing questions. The native session now does both.

## What it is

- **Service:** `BluetoothService` (`src/desktop/bluetooth.{hpp,cpp}`), in the desktop process.
  - Follows BlueZ on the system bus: `GetManagedObjects`, then `InterfacesAdded`,
    `InterfacesRemoved` and `PropertiesChanged`, accepted only from the owner of `org.bluez`.
  - Uses the first adapter (`hci0`). It shows adapter power, discovery and the adapter's name, and
    for each device its name, kind (from BlueZ's icon), connection, trust and battery
    (`Battery1.Percentage`).
  - Connects, disconnects, forgets (`RemoveDevice`), pairs, and turns the adapter on and off.
  - Every call is asynchronous, so the compositor never waits on the bus. Updates are coalesced
    (50 ms), and signal-strength changes during discovery are ignored.
  - Messages are plain, for example "“Mouse” could not connect. Make sure it is turned on and
    nearby." Only BlueZ's error name is logged.
- **Pairing agent:** `org.bluez.Agent1` at `/org/platinum_reality/BluetoothAgent`, capability
  `KeyboardDisplay`, as bluedevil registers.
  - It answers PIN, passkey, code confirmation, codes to type on the device, pairing
    authorization and service authorization.
  - Only the current owner of `org.bluez` may ask; anyone else gets `AccessDenied` (tested).
  - One question at a time. An overlapping question is declined, and BlueZ's `Cancel` closes the
    alert without an answer.
  - In the login session it also asks to be the **default** agent, so devices that connect by
    themselves are answered. Run as a window inside KDE, it registers but leaves the default to
    KDE.
- **After pairing from Add Device:** as KDE does, the device is marked Trusted (so it can reconnect
  by itself) and connected. Discovery stops while pairing, and turns itself off after two minutes.
- **Reconnection:** BlueZ reconnects trusted devices; devices such as mice and headsets reconnect
  themselves when turned on. A paired but untrusted device asks first: the alert's **Always allow
  this device** sets Trusted, like KDE's "Trust" action. MX Vertical and Govee were untrusted
  under KDE, so they will ask once.

## Where it appears

- **Apple menu > Control Panels > Bluetooth…**, a Platinum control panel (`BluetoothPanel.qml`).
  - Bluetooth: On/Off with **Turn Off/On**, and the name other devices see.
  - The paired devices: a glyph, the name, and "Headphones — Connected, battery 80%".
  - **Connect/Disconnect** (or double-click a device), **Forget…**, which asks first with Cancel
    as the default, and **Add Device…**.
  - Add Device looks for nearby devices in pairing mode. Only named devices are listed, at most
    50. Select one and click **Pair**.
- **Control Strip Bluetooth module:**
  - Open Bluetooth…, then "Bluetooth: On" and Turn Bluetooth Off/On.
  - The paired devices, with ✓ when connected and the battery level ("Arctis — 80%"). Choosing one
    connects or disconnects it.
  - Add Device….
- **Quick Controls:** Bluetooth power uses the same service, so it is live.
- **Pairing alerts** (`BluetoothAlert.qml`) appear whether or not the panel is open.
  - "Does “Speaker” show this code? Pair only if the codes match." **Cancel**/**Pair**.
  - "Enter its PIN. Many headsets and speakers use 0000." The field accepts 1–16 printable
    characters.
  - "Enter the passkey shown on …" (up to six digits).
  - "To pair with …, type this code on it", with digits typed so far. Return does not cancel it,
    and Cancel stops the pairing.
  - "“Keyboard” wants to connect to this computer." **Don’t Allow**/**Allow**, with **Always allow
    this device**.
  - Escape declines. Questions that arrive back to back, such as a headset's services, open one
    after another.

## Tests

- `tests/bluetooth_tests.cpp` (ctest `bluetooth`) uses `tests/fake_bluez.hpp`, a fake BlueZ (object
  manager, properties, Adapter1, Device1, Battery1, AgentManager1) on a private `dbus-daemon`. It
  covers:
  - the device list;
  - live battery and connection changes;
  - connect, disconnect, forget and power;
  - a connection failure message;
  - pairing with confirmation, then trust and connect;
  - a canceled code display;
  - PIN, passkey (and an invalid one), allow once/never/always;
  - an impostor;
  - an overlapping question and BlueZ's Cancel;
  - BlueZ starting late and restarting;
  - no bus at all;
  - a nested desktop not taking the default agent;
  - the real `Desktop.qml`: strip module, panel, Forget confirmation, Add Device and the alerts
    (screenshots `build/artifacts/control-strip-bluetooth.png`, `bluetooth-panel.png` and
    `bluetooth-pairing-alert.png`).
- `scripts/qualify-session.py`, scenario `bluetooth`: the installed login session against
  `tests/fake_bluez.py` on a private bus.
  - The desktop process (by PID) lists BlueZ's objects, registers `KeyboardDisplay` and becomes
    the default agent.
  - It refuses an impostor, leaves a confirmation waiting for the user (no answer within three
    seconds), accepts BlueZ's Cancel, and unregisters at logout.
- `tests/desktop_tests.cpp`: without BlueZ, the strip module shows "Bluetooth Unavailable" and
  never calls the Quick Controls helper.

## Safety

- Qualification never reaches the host's BlueZ. Gates set `DBUS_SYSTEM_BUS_ADDRESS` to a missing
  socket (or the fake's private bus), and the supervisor passes it to the desktop.
- Native tests without that variable do not connect the system bus at all
  (`FRANKENSTEIN_NATIVE_TEST` guard in `NativeManager`), so no test registers an agent with the
  host.
- Nothing here changed a real adapter or device. Hardware behavior (their headsets, mice, controller,
  speaker and the Govee light) needs psn_tch's feedback.

## Not yet

- Audio profile selection, file transfer (OBEX), adapter renaming, discoverable mode, and devices
  on a second adapter.
- Battery levels appear only where BlueZ reports them (`Battery1`).
- No notification when a device connects or its battery is low.
