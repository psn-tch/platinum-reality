# Control Strip quick settings

The fourth Control Strip module opens a compact classic-style settings dialog.
It provides active-display brightness, default audio volume/mute, Wi-Fi power
and Bluetooth power. Bluetooth power comes from the live Bluetooth service
(docs/bluetooth.md), not the helper. Full Control Panels remain separate. This is a modern
adaptation of the Control Strip idea, not authenticated System 7/9 behavior.

The Python helper uses installed tools with fixed argument vectors, without a
shell or elevated privileges. It supports NetworkManager or the existing IWD
service, BlueZ and PipeWire. Brightness follows exactly one active DRM connector:
the disabled internal panel is excluded, an external monitor uses its matching
DDC route, and ambiguous/unavailable devices are disabled. Brightness has a 5%
minimum. No device settings are queried until the user opens/refreshes the dialog.

The asynchronous bridge coalesces queued changes by control. Sliders retain the
newest requested position while an older helper reply arrives; completion
reconciles it with the actual device snapshot. Separate notifications for state,
busy and errors prevent starting a request from resetting a slider. A failed
request clears queued changes, shows an error and restores reported state.
Keyboard arrows commit on release; wheel changes are debounced. No production
hardware mutation is exercised by automatic tests.

Six Python cases use fake command replies. The real QML test uses a stateful,
delayed fake helper to verify successive arrow steps, changes while busy, final
backend value, retained keyboard focus, unavailable devices, normal Escape and
malformed replies. Actual machine status has been inspected read-only; successful
hardware changes, reconnect behavior and every device/service remain unqualified.

Historical reference: Apple's January 1996 Control Strip 1.3 Read Me, preserved
as [TIL 19249](https://savagetaylor.com/TIL/TIL19249.pdf), describes modules used
without opening Control Panels. It does not establish the modern Wi-Fi/Bluetooth
interfaces or this dialog as historical Apple behavior. No Apple artwork is bundled.
