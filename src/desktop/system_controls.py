#!/usr/bin/env python3
"""Small, explicit user-action adapter. No shell, privilege escalation or daemon.

Status is read-only. Mutations are only invoked by the Control Strip buttons.
Unknown/multiple monitor routes are unavailable rather than guessed.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def run(arguments):
    environment = os.environ.copy()
    # The compositor's private XDG runtime deliberately contains no host bus or
    # services. Address only the existing user's PipeWire socket for audio.
    if arguments[0] == "/usr/bin/wpctl":
        runtime = Path("/run/user") / str(os.getuid())
        if runtime.is_dir() and runtime.stat().st_uid == os.getuid():
            environment["PIPEWIRE_RUNTIME_DIR"] = str(runtime)
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=4,
                            check=False, env=environment)
    if result.returncode:
        raise RuntimeError("The device service refused the request or is unavailable.")
    if len(result.stdout) > 262144:
        raise RuntimeError("The device service returned an oversized response.")
    return result.stdout.strip()


def powered(service, path, interface):
    value = run(["/usr/bin/busctl", "--system", "get-property", service, path,
                 interface, "Powered"])
    if value not in ("b true", "b false"):
        raise RuntimeError("The device power state is unavailable.")
    return value == "b true"


def active_connectors(drm=Path("/sys/class/drm")):
    return [p.name.split("-", 1)[1] for p in sorted(drm.glob("card*-*"))
            if (p / "status").is_file() and (p / "enabled").is_file()
            and (p / "status").read_text().strip() == "connected"
            and (p / "enabled").read_text().strip() == "enabled"]


def volume():
    text = run(["/usr/bin/wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"])
    match = re.fullmatch(r"Volume: ([0-9]+(?:\.[0-9]+)?)( \[MUTED\])?", text)
    if not match:
        raise RuntimeError("No default audio output is available.")
    return dict(available=True, value=min(100, round(float(match[1]) * 100)),
                muted=bool(match[2]))


def brightness():
    connectors = active_connectors()
    if len(connectors) != 1:
        raise RuntimeError("Brightness needs exactly one active display.")
    connector = connectors[0]
    if connector.startswith(("eDP-", "LVDS-")):
        devices = sorted(Path("/sys/class/backlight").iterdir())
        if len(devices) != 1:
            raise RuntimeError("The internal display brightness device is ambiguous.")
        device = devices[0]
        maximum = int((device / "max_brightness").read_text())
        current = int((device / "brightness").read_text())
        return dict(available=True, value=round(100 * current / maximum),
                    device=connector, backend="backlight", target=device.name)
    text = run(["/usr/bin/ddcutil", "detect", "--brief"])
    matches = []
    for section in re.split(r"(?m)(?=^Display \d+)", text):
        display = re.match(r"Display (\d+)\b", section)
        if display and re.search(r"card\d+-" + re.escape(connector) + r"\b", section):
            matches.append(display[1])
    if len(matches) != 1:
        raise RuntimeError("The active monitor has no unambiguous DDC brightness route.")
    text = run(["/usr/bin/ddcutil", "getvcp", "10", "--brief", "--display", matches[0]])
    match = re.search(r"\bVCP 10 C (\d+) (\d+)\b", text)
    if not match or int(match[2]) <= 0:
        raise RuntimeError("The monitor did not report a brightness range.")
    return dict(available=True, value=round(100 * int(match[1]) / int(match[2])),
                device=connector, backend="ddc", target=matches[0], maximum=int(match[2]))


def wifi():
    try:
        state = run(["/usr/bin/nmcli", "radio", "wifi"])
        if state in ("enabled", "disabled"):
            return dict(available=True, enabled=state == "enabled", backend="nm")
    except (OSError, RuntimeError, subprocess.TimeoutExpired):
        pass
    # Do not emit the tree: IWD object paths can include nearby network names.
    paths = run(["/usr/bin/busctl", "--system", "--list", "tree", "net.connman.iwd"])
    adapters = [p for p in paths.splitlines() if re.fullmatch(r"/net/connman/iwd/\d+", p)]
    if len(adapters) != 1:
        raise RuntimeError("Wi-Fi needs one available adapter.")
    return dict(available=True, enabled=powered("net.connman.iwd", adapters[0],
                "net.connman.iwd.Adapter"), backend="iwd", target=adapters[0])


def bluetooth():
    paths = run(["/usr/bin/busctl", "--system", "--list", "tree", "org.bluez"])
    adapters = [p for p in paths.splitlines() if re.fullmatch(r"/org/bluez/hci\d+", p)]
    if len(adapters) != 1:
        raise RuntimeError("Bluetooth needs one available adapter.")
    return dict(available=True, enabled=powered("org.bluez", adapters[0],
                "org.bluez.Adapter1"), target=adapters[0])


READERS = dict(volume=volume, brightness=brightness, wifi=wifi, bluetooth=bluetooth)


def snapshot():
    result = {}
    for name, reader in READERS.items():
        try:
            result[name] = reader()
        except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
            result[name] = dict(available=False)
    return result


def change(name, value):
    if name not in ("volume", "mute", "brightness", "wifi", "bluetooth"):
        raise ValueError("Unknown control.")
    if type(value) is not int or not 0 <= value <= (100 if name in ("volume", "brightness") else 1):
        raise ValueError("Invalid control value.")
    if name == "volume":
        run(["/usr/bin/wpctl", "set-volume", "--limit", "1.0", "@DEFAULT_AUDIO_SINK@", str(value) + "%"])
    elif name == "mute":
        run(["/usr/bin/wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", str(value)])
    elif name == "brightness":
        route = brightness()
        # Never black out the display through this small convenience widget.
        value = max(5, value)
        if route["backend"] == "ddc":
            run(["/usr/bin/ddcutil", "setvcp", "10", str(round(value * route["maximum"] / 100)),
                 "--display", route["target"]])
        else:
            run(["/usr/bin/brightnessctl", "--device", route["target"], "set", str(value) + "%"])
    else:
        state = READERS[name]()
        if state.get("backend") == "nm":
            run(["/usr/bin/nmcli", "radio", "wifi", "on" if value else "off"])
        else:
            service, interface = (("net.connman.iwd", "net.connman.iwd.Adapter") if name == "wifi"
                                  else ("org.bluez", "org.bluez.Adapter1"))
            run(["/usr/bin/busctl", "--system", "set-property", service, state["target"],
                 interface, "Powered", "b", "true" if value else "false"])


def main():
    try:
        if len(sys.argv) == 2 and sys.argv[1] == "status":
            error = ""
        elif len(sys.argv) == 4 and sys.argv[1] == "set":
            change(sys.argv[2], int(sys.argv[3]))
            error = ""
        else:
            raise ValueError("Invalid control request.")
        print(json.dumps(dict(state=snapshot(), error=error)))
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
        # Do not expose raw command output, adapter/network names or credentials.
        print(json.dumps(dict(error="The setting could not be changed. Check device availability and permissions.")))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
