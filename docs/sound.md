# Sound

The Sound control panel (Rocky's hardened design, integrated 2026-10-21; details and the
failed-before evidence are in `experiments/audio-routing-hardening/README.md`).

## What it is

- **Where:** Apple menu > Control Panels > Sound…, and the Control Strip Sound module's first
  item, Open Sound…. The module still offers Mute and the volume steps; Quick Controls keeps its
  volume slider.
- **Panel** (`src/desktop/SoundPanel.qml`): Alert Sounds, Output and Input tabs; devices listed as radio
  buttons ("Bluetooth — Selected"), a volume slider with Mute/Unmute, and a Mode menu for cards
  with profiles (for example "Headset with Microphone").
- **Service** (`AudioService`, `manager.audio`):
  - Queries only when the panel opens.
  - Runs one asynchronous helper at a time; the newest choice per control wins.
- **Backend** (`audio_controls.py`):
  - `pactl` with fixed arguments: no shell, no elevation.
  - Output monitors are not offered as microphones.
  - Choosing a device moves only the streams on the previous default, so applications routed
    elsewhere stay put.
  - It waits up to two seconds for the session manager to apply the new default or profile.
  - It runs in its own process group with a 2 MiB reply cap.
- **Audio server:** the login session names it (`PULSE_SERVER`). When Platinum Reality runs as a
  window inside another desktop, the helper falls back to the user's own
  `/run/user/<uid>/pulse/native` (owned by the user, a real socket). It never replaces a server
  the session named.

## Tests

- **`audio-controls`** (22): Python with fake `pactl`, including the fallback to the user's own
  audio socket.
- **`audio-service`** and **`sound-integration`:** offscreen QML with fake helpers. Screenshots
  are in `build/artifacts/audio-service/`.
- **`desktop`:** Quick Controls is now reached through the Brightness module, because Sound's
  first item opens the Sound panel.
- **Install check** (`qualify-install.py`, 3 checks): the *installed* `audio_controls.py`
  against `tests/owned_audio_server.py`, a disposable PipeWire with virtual speakers,
  headphones and a microphone. There are no hardware modules, no WirePlumber, and no host bus
  or audio socket.
  - It lists the devices, without monitors.
  - It switches the output, sets the volume to 37% and mutes the microphone.
  - The server is reaped afterwards.

The selected Bluetooth-launcher release includes Sound. See the current
[handoff](../HANDOFF.md) and [alert sounds](alert-sounds.md) for qualification and
personal-asset behavior.

## Not yet

- Real WirePlumber settling, Bluetooth headset modes, and physical devices on hardware.
- Per-application volume and a direct native panel workflow against owned virtual audio
  (the installed helper and offscreen panel have separate coverage).
