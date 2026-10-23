# Wi-Fi

The Control Strip's Wi-Fi module joins networks, as impala does, through **iwd** on the system
bus (`src/desktop/wifi.cpp`). Turning Wi-Fi on and off stays with the controls helper
(`system_controls.py`, iwd's Adapter `Powered`).

- **The module's menu** lists the networks the first station sees, strongest first (iwd's
  `Station.GetOrderedNetworks`), up to 12, with four signal bars (impala's thresholds: -60,
  -67 and -75 dBm) and a lock for secured networks. The joined network is checked. Choosing
  another joins it. Opening the module asks iwd to scan. Hidden networks are not listed.
- **Passwords:** the desktop registers as an iwd agent. When iwd joins a secured network it
  does not know, it asks the agent, and a Platinum alert asks for the password ("Show
  password" reveals it; a WPA password has 8 to 63 characters, or 64 hexadecimal digits).
  Company and school networks ask for a user name too. iwd remembers what was joined, the
  same store impala uses. Only iwd may ask the agent; other programs on the bus are refused.
- **Disconnect from**, **Forget…** (asks first; Cancel is the default) and **Scan for Networks**.
- Network names come from the radio: control and formatting characters become spaces, and
  names are cut at 64 characters.

Only a real session talks to the host's iwd (the same rule as Bluetooth). Tests use a fake iwd
on a private bus (`tests/fake_iwd.hpp`, `tests/wifi_tests.cpp`); nothing in them reaches the
host's Wi-Fi.
