#!/usr/bin/env python3
"""Experimental login-session supervisor. Does not change login configuration.

Sway supplies DRM/seat/input/session-lock; Platinum Reality owns the desktop.
Use --nested-check to qualify the same lifecycle without taking a physical seat.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import select
import secrets
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

CONFIG = """# Private backend: never include the user's Sway configuration.
xwayland disable
swaybg_command -
swaynag_command -
default_border none
default_floating_border none
focus_follows_mouse no
mouse_warping none
# This binding only locks/refreshes; it cannot authenticate or unlock. Allow it
# while locked as a recovery key, without repeated refreshes when the key is held.
bindsym --locked --inhibited --no-repeat Ctrl+Alt+l nop platinum-lock
# Laptop touchpads: tap to click and two-finger scrolling, as psn_tch has them under KDE (2026-10-20).
input type:touchpad {
    tap enabled
    scroll_method two_finger
}
# The built-in touchscreen follows the built-in panel, so touches turn with its rotation.
input type:touch {
    map_to_output eDP-1
}
"""
HEADER = struct.Struct("=6sII")
# --start-locked: the user chose KDE at Log In (login-session.py then starts it).
KDE_SESSION_EXIT = 10
MAX_IPC = 1024 * 1024


def choose_primary_output(outputs, preferred):
    """Honor a connected connector; otherwise prefer the largest external screen.

    Connector numbers can change when docking. Never interpolate an unvalidated
    backend-provided name into a Sway command, or choose a non-desktop display.
    """
    candidates = [output for output in outputs
                  if re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", output.get("name", ""))
                  and not output.get("non_desktop", False)]
    for output in candidates:
        if output["name"] == preferred:
            return output, "requested_connector"
    external = [output for output in candidates
                if not output["name"].lower().startswith(("edp-", "lvds-", "dsi-"))]

    def area(output):
        mode = output.get("current_mode") or {}
        if not mode:
            mode = max(output.get("modes") or [{}],
                       key=lambda item: item.get("width", 0) * item.get("height", 0))
        return max(0, mode.get("width", 0)) * max(0, mode.get("height", 0))

    pool = external or candidates
    if not pool:
        raise RuntimeError("No usable desktop output is connected")
    selected = sorted(pool, key=lambda output: (-area(output), output["name"]))[0]
    return selected, "fallback_external" if external else "fallback_internal"


# KWin counts rotation the opposite way to Sway: psn_tch's panel, Rotated90 in KDE, is upright with
# Sway's "270" (confirmed on the real screen, 2026-10-20). Flipped quarter-turns follow suit.
KWIN_TRANSFORMS = {"Normal": "normal", "Rotated90": "270", "Rotated180": "180", "Rotated270": "90",
                   "Flipped": "flipped", "Flipped90": "flipped-270", "Flipped180": "flipped-180",
                   "Flipped270": "flipped-90"}


# psn_tch's OneXPlayer panel is portrait inside a landscape laptop. Their exact command, made the
# default (2026-10-20): output eDP-1 transform 270 scale 2.3. Matched by panel model as well,
# so another laptop's eDP-1 is never turned. KDE's own setting still wins when it has one.
PANEL_DEFAULTS = {("eDP-1", "HSX", "YHB02P24"): {"transform": "270", "scale": 2.3}}


def panel_defaults(outputs):
    """Built-in rotation and scale for known panels; read-only and connector-checked."""
    preferences = {}
    for output in outputs:
        default = PANEL_DEFAULTS.get((output.get("name"), output.get("make"), output.get("model")))
        if default:
            preferences[output["name"]] = dict(default)
    return preferences


def sysfs_edid_hash(name, root=Path("/sys/class/drm")):
    """The MD5 of a connected monitor's EDID, which is how KWin remembers it (read-only)."""
    matches = [entry / "edid" for entry in root.glob("card*-" + name)
               if entry.name.split("-", 1)[1:] == [name]]
    try:
        if len(matches) != 1 or not matches[0].is_file():
            return None
        with matches[0].open("rb") as edid:
            data = edid.read(32 * 1024 + 1)
    except OSError:
        return None
    if not data or len(data) > 32 * 1024:
        return None
    return hashlib.md5(data).hexdigest()


def main_display_identity(output, edid_hash=sysfs_edid_hash):
    """Remember a monitor, not merely the dock port it occupied. No output commands."""
    name = output.get("name", "")
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
        raise ValueError("Invalid main display connector")
    identity = {"version": 1, "connector": name}
    edid = edid_hash(name)
    if isinstance(edid, str) and re.fullmatch(r"[0-9a-f]{32}", edid):
        identity["edid"] = edid
    for key in ("make", "model", "serial"):
        value = output.get(key)
        if isinstance(value, str) and value and len(value) <= 256:
            identity[key] = value
    return identity


def saved_main_display(profile):
    """Small, regular, user-owned profile only; never follow a settings symlink/FIFO."""
    try:
        fd = os.open(Path(profile) / "main-display.json", os.O_RDONLY | os.O_CLOEXEC |
                     os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_size > 4096:
                return None
            data = json.loads(stream.read(4097))
    except (OSError, ValueError, UnicodeError):
        return None
    if (not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1
            or not isinstance(data.get("connector"), str)
            or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", data["connector"])
            or any(not isinstance(data[k], str) or not 0 < len(data[k]) <= 256
                   for k in ("make", "model", "serial") if k in data)
            or ("edid" in data and (not isinstance(data["edid"], str)
                                    or not re.fullmatch(r"[0-9a-f]{32}", data["edid"])))):
        return None
    return {k: v for k, v in data.items() if k in ("version", "connector", "edid", "make", "model", "serial")}


def resolve_main_display(outputs, saved, edid_hash=sysfs_edid_hash, active_only=True):
    """Return one active (or, with active_only=False, connected) match, or None. A missing
    preference is never overwritten.

    EDID wins over connector names. Identical EDIDs require the original port;
    without EDID, a useful serial can follow a port change. Identity-less virtual
    outputs can use their connector, but known metadata must still agree.
    """
    if not saved:
        return None
    candidates = [main_display_identity(o, edid_hash) for o in outputs
                  if (o.get("active") is True or not active_only) and not o.get("non_desktop", False)
                  and isinstance(o.get("name"), str)
                  and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o["name"])]
    if saved.get("edid"):
        matches = [c for c in candidates if c.get("edid") == saved["edid"]]
    else:
        keys = [k for k in ("make", "model", "serial") if k in saved]
        matches = [c for c in candidates if all(c.get(k) == saved[k] for k in keys)]
        serial = saved.get("serial", "").strip().lower()
        if not serial or serial in ("unknown", "none", "0", "0x00000000"):
            matches = [c for c in matches if c["connector"] == saved["connector"]]
    if len(matches) != 1:
        matches = [c for c in matches if c["connector"] == saved["connector"]]
    return matches[0]["connector"] if len(matches) == 1 else None


def save_private_setting(path, data):
    """Atomic owner-only settings file. Errors propagate so a control request can roll back
    instead of claiming success; the previous file is never truncated or followed."""
    try:
        info = path.lstat()
    except FileNotFoundError:
        info = None
    if info and (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()):
        raise OSError("The display setting is not a regular user-owned file")
    fd, temporary = tempfile.mkstemp(prefix="." + path.stem + "-", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(data, stream, ensure_ascii=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def save_main_display(profile, identity):
    """Separate ownership from the Monitors panel's displays.json, which this never touches."""
    save_private_setting(Path(profile) / "main-display.json", identity)


def kde_output_preferences(path, outputs, edid_hash=sysfs_edid_hash):
    """Each screen's rotation and scale as the user set them in KDE (read-only).

    KWin's quarter-turns run opposite to Sway's (see KWIN_TRANSFORMS). A monitor is matched by
    its EDID first: dock ports change, and psn_tch's Samsung on DP-3 took the scale KDE kept for a
    different monitor once seen on DP-3. Only a monitor KDE has never seen by EDID falls back to
    its connector name. Several candidates are told apart by the current mode's size;
    anything ambiguous or malformed is ignored.
    """
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 1024 * 1024:
            return {}
        sections = json.loads(path.read_text())
    except (OSError, ValueError, UnicodeError):
        return {}
    if not isinstance(sections, list):
        return {}
    entries = [entry for section in sections if isinstance(section, dict) and section.get("name") == "outputs"
               and isinstance(section.get("data"), list) for entry in section["data"] if isinstance(entry, dict)]
    preferences = {}
    for output in outputs:
        name = output.get("name", "")
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
            continue
        mode = output.get("current_mode") or {}
        identity = edid_hash(name)
        matches = [entry for entry in entries if identity and entry.get("edidHash") == identity]
        if not matches:
            matches = [entry for entry in entries if entry.get("connectorName") == name
                       and not (identity and entry.get("edidHash"))]
        if len(matches) > 1:
            matches = [entry for entry in matches if isinstance(entry.get("mode"), dict)
                       and entry["mode"].get("width") == mode.get("width")
                       and entry["mode"].get("height") == mode.get("height")]
        if len(matches) != 1:
            continue
        transform = KWIN_TRANSFORMS.get(matches[0].get("transform"))
        scale = matches[0].get("scale")
        if transform is None or isinstance(scale, bool) or not isinstance(scale, (int, float)) \
                or not 0.5 <= scale <= 4:
            continue
        preferences[name] = {"transform": transform, "scale": round(float(scale), 4)}
    return preferences


def lid_closed(root=Path("/proc/acpi/button/lid")):
    """True/False from the one ACPI lid switch, or None without exactly one (read-only)."""
    try:
        states = [entry.joinpath("state").read_text() for entry in root.iterdir()]
    except OSError:
        return None
    return "closed" in states[0] if len(states) == 1 else None


def kde_arrangement(path, outputs, edid_hash=sysfs_edid_hash, lid=None):
    """The arrangement KDE saved for exactly this set of monitors (read-only).

    KWin keeps one "setup" per combination of connected monitors and lid state, naming monitors
    by their index in its "outputs" list. Monitors are identified as in kde_output_preferences()
    (EDID first, else a connector name no known monitor claims). Returns
    {connector: {"enabled", "x", "y", "primary"}} for the one setup covering exactly the connected
    monitors, preferring the current lid state; {} if KDE never saw this combination or anything
    is ambiguous. The primary is the enabled monitor with KWin's lowest positive priority.
    """
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 1024 * 1024:
            return {}
        sections = json.loads(path.read_text())
    except (OSError, ValueError, UnicodeError):
        return {}
    if not isinstance(sections, list):
        return {}

    def section(name):
        return next((s["data"] for s in sections if isinstance(s, dict) and s.get("name") == name
                     and isinstance(s.get("data"), list)), [])

    entries, setups = section("outputs"), section("setups")
    indices = {}
    for output in outputs:
        name = output.get("name", "")
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name) or output.get("non_desktop", False):
            continue
        identity = edid_hash(name)
        matches = [i for i, e in enumerate(entries) if isinstance(e, dict) and identity
                   and e.get("edidHash") == identity]
        if not matches:
            matches = [i for i, e in enumerate(entries) if isinstance(e, dict)
                       and e.get("connectorName") == name and not (identity and e.get("edidHash"))]
        if len(matches) != 1:
            return {}
        indices[matches[0]] = name
    candidates = []
    for setup in setups:
        if not isinstance(setup, dict) or not isinstance(setup.get("outputs"), list):
            continue
        placed = {}
        for item in setup["outputs"]:
            position = item.get("position") if isinstance(item, dict) else None
            if (not isinstance(item, dict) or type(item.get("outputIndex")) is not int
                    or not isinstance(item.get("enabled"), bool) or not isinstance(position, dict)
                    or any(type(position.get(k)) is not int or abs(position[k]) > 65536 for k in ("x", "y"))):
                placed = None
                break
            placed[item["outputIndex"]] = item
        if placed and set(placed) == set(indices) and any(i["enabled"] for i in placed.values()):
            candidates.append((setup.get("lidClosed") is lid, placed))
    same_lid = [placed for matches_lid, placed in candidates if matches_lid]
    chosen = same_lid if lid is not None and same_lid else [placed for _, placed in candidates]
    if len(chosen) != 1:
        return {}
    placed = chosen[0]
    primary = min((item for item in placed.values() if item["enabled"]),
                  key=lambda item: (item["priority"] if type(item.get("priority")) is int and item["priority"] > 0
                                    else 1 << 30, item["outputIndex"]))
    return {indices[index]: {"enabled": item["enabled"], "x": item["position"]["x"], "y": item["position"]["y"],
                             "primary": item is primary} for index, item in placed.items()}


def desktop_profile(profile, environment):
    """The launcher's desktop profile: --profile, else $XDG_DATA_HOME/platinum-reality/profile."""
    if profile:
        return Path(profile).expanduser().absolute()
    return Path(environment.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "platinum-reality/profile"


def connected_display_names(outputs, lid=None):
    """Desktop connectors to enable for an explicit Use-all choice.

    A shut laptop panel is not useful beside an external monitor. Never leave
    zero outputs, and never treat a VR/non-desktop connector as a desktop.
    """
    names = [o.get("name") for o in outputs if isinstance(o, dict)
             and isinstance(o.get("name"), str)
             and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o["name"])
             and not o.get("non_desktop", False)]
    if not names or len(names) > 16 or len(set(names)) != len(names):
        raise ValueError("Invalid connected desktop inventory")
    external = [n for n in names if not n.lower().startswith(("edp-", "lvds-", "dsi-"))]
    return sorted(external if lid is True and external else names)


def extended_connected_arrangement(outputs, seed, preferred, lid=None, wanted=None):
    """Fit all chosen, already-enabled outputs without overlap, using actual logical sizes.

    Preserve usable KDE positions as seeds, but not its disabled flags. Newly
    enabled screens that overlap a seed go to its right. Rotation/scale must be
    applied before this function; it never guesses a disabled output's size.
    """
    wanted = connected_display_names(outputs, lid) if wanted is None else sorted(wanted)
    by_name = {o["name"]: o for o in outputs if isinstance(o, dict) and o.get("name") in wanted}
    seeded_primary = next((n for n in wanted if seed.get(n, {}).get("primary")), preferred)
    primary, _ = choose_primary_output([by_name[n] for n in wanted], seeded_primary)
    placed = {}
    ordered = sorted(wanted, key=lambda n: (not seed.get(n, {}).get("enabled", False),
                                            n != primary["name"], n))
    for name in ordered:
        output = by_name[name]
        rect = output.get("rect", {})
        if (not output.get("active") or any(type(rect.get(k)) is not int for k in ("x", "y", "width", "height"))
                or rect["width"] < 80 or rect["height"] < 80):
            raise ValueError("A connected display has no usable enabled geometry")
        source = seed[name] if seed.get(name, {}).get("enabled") else rect
        x, y, w, h = source["x"], source["y"], rect["width"], rect["height"]
        if placed and (not seed.get(name, {}).get("enabled") or
                       any(x < px + pw and px < x + w and y < py + ph and py < y + h
                           for px, py, pw, ph in placed.values())):
            x = max(px + pw for px, py, pw, ph in placed.values())
            y = min(py for px, py, pw, ph in placed.values())
        placed[name] = x, y, w, h
    left = min(r[0] for r in placed.values())
    top = min(r[1] for r in placed.values())
    result = {name: {"enabled": True, "x": r[0] - left, "y": r[1] - top,
                      "primary": name == primary["name"]} for name, r in placed.items()}
    # Reuse the consumer's bounds/canvas/overlap contract before sending positions.
    proposed = [{**by_name[n], "rect": {**by_name[n]["rect"], "x": p["x"], "y": p["y"]}}
                for n, p in result.items()]
    describe_displays(proposed, primary["name"])
    return result


def restore_display_commands(outputs):
    """A failed activation restores old usable outputs before retiring new ones."""
    active = [o for o in outputs if o.get("active") and not o.get("non_desktop", False)]
    if not active:
        return []  # No prior visible screen: never blank the only newly available one.
    describe_displays(active, None)  # Validate names, scale, transform and geometry first.
    commands = []
    for output in active:
        name, rect = output["name"], output["rect"]
        commands += [f"output {name} enable", f"output {name} transform {output['transform']}",
                     f"output {name} scale {output['scale']}",
                     f"output {name} position {rect['x']} {rect['y']}"]
    commands += [f"output {o['name']} disable" for o in outputs
                 if not o.get("active") and not o.get("non_desktop", False)
                 and isinstance(o.get("name"), str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o["name"])]
    return commands


def saved_display_settings(profile):
    """The Monitors control panel's choices: "mode" is "extend" (every connected display shows
    the desktop, except the monitors in "off") or "single" (only the main display); "displays"
    holds each monitor's chosen resolution, size (scale) and rotation. Read-only and bounded;
    anything unexpected is one display with nothing switched off or changed."""
    path = Path(profile) / "displays.json"
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 16384:
            return {"mode": "single", "off": [], "displays": []}
        data = json.loads(path.read_text())
    except (OSError, ValueError, UnicodeError):
        return {"mode": "single", "off": [], "displays": []}
    if not isinstance(data, dict):
        return {"mode": "single", "off": [], "displays": []}
    off = [identity for identity in map(valid_display_identity, data.get("off", [])
                                        if isinstance(data.get("off"), list) else []) if identity is not None]
    displays = []
    for entry in data.get("displays", []) if isinstance(data.get("displays"), list) else []:
        if not isinstance(entry, dict):
            continue
        identity = valid_display_identity(entry.get("identity"))
        if identity is None:
            continue
        setting = {"identity": identity}
        mode = entry.get("mode")
        if (isinstance(mode, dict) and all(type(mode.get(k)) is int for k in ("width", "height", "refresh"))
                and 80 <= mode["width"] <= 16384 and 80 <= mode["height"] <= 16384 and 0 <= mode["refresh"] <= 1000000):
            setting["mode"] = {k: mode[k] for k in ("width", "height", "refresh")}
        if type(entry.get("scale")) in (int, float) and math.isfinite(entry["scale"]) and 0.5 <= entry["scale"] <= 4:
            setting["scale"] = float(entry["scale"])
        if entry.get("transform") in DISPLAY_ROTATIONS:
            setting["transform"] = entry["transform"]
        position = entry.get("position")
        if (isinstance(position, dict) and all(type(position.get(k)) is int and -16384 <= position[k] <= 16384
                                               for k in ("x", "y"))):
            setting["position"] = {"x": position["x"], "y": position["y"]}
        if len(setting) > 1:
            displays.append(setting)
    mode = data.get("mode") if data.get("mode") in ("extend", "mirror") else "single"
    return {"mode": mode, "off": off[:16], "displays": displays[:16]}


# The Monitors panel's rotations (Sway transforms); flipped ones are left to KDE's settings.
DISPLAY_ROTATIONS = ("normal", "90", "180", "270")


def valid_display_identity(identity):
    """A saved monitor identity (as main_display_identity makes), or None."""
    if (isinstance(identity, dict) and isinstance(identity.get("connector"), str)
            and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", identity["connector"])
            and all(isinstance(identity[k], str) and 0 < len(identity[k]) <= 256
                    for k in ("make", "model", "serial") if k in identity)
            and ("edid" not in identity or isinstance(identity["edid"], str)
                 and re.fullmatch(r"[0-9a-f]{32}", identity["edid"]))):
        return {k: v for k, v in identity.items() if k in ("version", "connector", "edid", "make", "model", "serial")}
    return None


def saved_display_mode(profile):
    return saved_display_settings(profile)["mode"]


def save_display_settings(profile, mode, off, displays=()):
    """Version 2 keeps "mode", so an older release still reads the Use-all choice."""
    save_private_setting(Path(profile) / "displays.json",
                         {"version": 2, "mode": mode if mode in ("extend", "mirror") else "single", "off": off[:16],
                          "displays": list(displays)[:16]})


# The Keyboard and Mouse & Trackpad control panels' choices (the desktop writes input.json in its
# profile). Each value is checked against its range; anything missing or unexpected leaves Sway's
# own setting (and the touchpad defaults in CONFIG) as it is.
INPUT_RANGES = {
    ("keyboard", "repeatDelay"): (int, 150, 1500),
    ("keyboard", "repeatRate"): (int, 1, 60),
    ("pointer", "speed"): (float, -1.0, 1.0),
    ("pointer", "natural"): (bool, None, None),
    ("pointer", "leftHanded"): (bool, None, None),
    ("touchpad", "speed"): (float, -1.0, 1.0),
    ("touchpad", "natural"): (bool, None, None),
    ("touchpad", "tap"): (bool, None, None),
    ("touchpad", "disableWhileTyping"): (bool, None, None),
}


def saved_input_settings(profile):
    """input.json, read-only and bounded: {section: {key: value}} with only valid values."""
    path = Path(profile) / "input.json"
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 4096:
            return {}
        data = json.loads(path.read_text())
    except (OSError, ValueError, UnicodeError):
        return {}
    if not isinstance(data, dict) or data.get("version") != 1:
        return {}
    settings = {}
    for (section, key), (kind, low, high) in INPUT_RANGES.items():
        group = data.get(section)
        value = group.get(key) if isinstance(group, dict) else None
        if kind is bool:
            if type(value) is not bool:
                continue
        elif kind is int:
            if type(value) is not int or not low <= value <= high:
                continue
        elif type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
            continue
        settings.setdefault(section, {})[key] = float(value) if kind is float else value
    return settings


def input_commands(settings):
    """Sway commands for saved_input_settings(): keyboards, mice ("pointer") and touchpads. The
    left-handed choice swaps the buttons of both, as Mac OS 9's Mouse panel had one setting."""
    def on(value):
        return "enabled" if value else "disabled"
    commands = []
    keyboard = settings.get("keyboard", {})
    if "repeatDelay" in keyboard:
        commands.append(f"input type:keyboard repeat_delay {keyboard['repeatDelay']}")
    if "repeatRate" in keyboard:
        commands.append(f"input type:keyboard repeat_rate {keyboard['repeatRate']}")
    pointer, touchpad = settings.get("pointer", {}), settings.get("touchpad", {})
    for kind, group in (("pointer", pointer), ("touchpad", touchpad)):
        if "speed" in group:
            commands.append(f"input type:{kind} pointer_accel {group['speed']:.2f}")
        if "natural" in group:
            commands.append(f"input type:{kind} natural_scroll {on(group['natural'])}")
        if "leftHanded" in pointer:
            commands.append(f"input type:{kind} left_handed {on(pointer['leftHanded'])}")
    if "tap" in touchpad:
        commands.append(f"input type:touchpad tap {on(touchpad['tap'])}")
    if "disableWhileTyping" in touchpad:
        commands.append(f"input type:touchpad dwt {on(touchpad['disableWhileTyping'])}")
    return commands


def apply_input_settings(backend_ipc, profile, state):
    """Apply input.json to the private backend. Never raises: a failure is recorded in the state
    and the devices keep their previous settings. A command for a kind of device that is not
    connected (no touchpad) succeeds in Sway and simply has nothing to change."""
    commands = input_commands(saved_input_settings(profile))
    if not commands:
        state.pop("input_error", None)
        return True
    try:
        response = ipc(backend_ipc, 0, "; ".join(commands))
        ok = isinstance(response, list) and len(response) == len(commands) and all(
            isinstance(r, dict) and r.get("success") for r in response)
    except (OSError, ValueError) as error:
        state["input_error"] = str(error)[:200]
        return False
    if ok:
        state.pop("input_error", None)
        state["input_applied"] = len(commands)
    else:
        state["input_error"] = "Sway refused an input setting"
    return ok


def display_modes(output):
    """A display's distinct modes, largest and fastest first, and its current one (refresh in mHz)."""
    def mode(m):
        if (isinstance(m, dict) and all(type(m.get(k)) is int for k in ("width", "height", "refresh"))
                and 80 <= m["width"] <= 16384 and 80 <= m["height"] <= 16384 and 0 <= m["refresh"] <= 1000000):
            return {k: m[k] for k in ("width", "height", "refresh")}
        return None
    modes = {tuple(m.values()): m for m in map(mode, output.get("modes") or []) if m}
    ordered = [modes[k] for k in sorted(modes, key=lambda k: (-k[0] * k[1], -k[0], -k[2]))][:64]
    current = mode(output.get("current_mode"))
    return {"modes": ordered, **({"mode": current} if current else {})}


def mirror_plan(outputs, main):
    """Mirror: every other display that is on shows the main display. Each one gets the scale at
    which the main display's whole picture fits inside it, centred over it, so it shows the same
    desktop with bars on the sides whose shape differs. Returns {name: {"x", "y", "scale"}} for
    the others (the main display stays at 0,0 as it is). No output commands."""
    by_name = {o["name"]: o for o in outputs if o.get("active") is True and not o.get("non_desktop", False)
               and isinstance(o.get("name"), str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o["name"])}
    if main not in by_name:
        raise ValueError("The main display is not on")
    rect = by_name[main].get("rect") or {}
    if any(type(rect.get(k)) is not int or rect[k] < 80 for k in ("width", "height")):
        raise ValueError("The main display has no usable size")
    width, height = rect["width"], rect["height"]
    plan = {}
    for name, output in sorted(by_name.items()):
        if name == main:
            continue
        mode = output.get("current_mode") or {}
        if any(type(mode.get(k)) is not int or mode[k] < 80 for k in ("width", "height")):
            raise ValueError("A mirrored display has no usable mode")
        across, down = mode["width"], mode["height"]
        if output.get("transform") in ("90", "270", "flipped-90", "flipped-270"):
            across, down = down, across
        scale = max(0.5, min(4.0, min(across / width, down / height)))
        scale = math.floor(scale * 120) / 120  # Wayland's fractional-scale steps; never larger than fits.
        logical_w, logical_h = round(across / scale), round(down / scale)
        plan[name] = {"x": -((logical_w - width) // 2), "y": -((logical_h - height) // 2), "scale": scale}
    return plan


def parse_display_setting(kind, text):
    """A control request's value: "1920x1080@60000" (refresh in mHz), "1.5", a rotation, or "X,Y"."""
    if kind == "mode":
        match = re.fullmatch(r"([0-9]{2,5})x([0-9]{2,5})@([0-9]{1,7})", text)
        return ({"width": int(match[1]), "height": int(match[2]), "refresh": int(match[3])} if match else None)
    if kind == "scale":
        if not re.fullmatch(r"[0-9]\.[0-9]{1,3}|[0-9]", text):
            return None
        return float(text) if 0.5 <= float(text) <= 4 else None
    if kind == "position":
        match = re.fullmatch(r"(-?[0-9]{1,5}),(-?[0-9]{1,5})", text)
        if match and all(-16384 <= int(v) <= 16384 for v in match.groups()):
            return {"x": int(match[1]), "y": int(match[2])}
        return None
    return text if kind == "rotate" and text in DISPLAY_ROTATIONS else None


def mode_command(name, mode):
    return f"output {name} mode {mode['width']}x{mode['height']}@{mode['refresh'] / 1000:.3f}Hz"


def switched_off(outputs, off, edid_hash=sysfs_edid_hash):
    """Connected connectors whose monitor the Monitors panel switched off."""
    names = (resolve_main_display(outputs, identity, edid_hash, active_only=False) for identity in off)
    return {name for name in names if name}


def chosen_display_names(outputs, lid, mode, off, main, edid_hash=sysfs_edid_hash):
    """The connected displays that show the desktop: only the main one ("single"), or all but
    the switched-off ones ("extend"). Never none: the main display (else the usual choice)
    stays on when everything else is off. No output commands."""
    connected = connected_display_names(outputs, lid)
    usable = [o for o in outputs if o.get("name") in connected]
    keep = [main] if main in connected else [choose_primary_output(usable, main)[0]["name"]]
    if mode not in ("extend", "mirror"):
        return keep
    disabled = switched_off(outputs, off, edid_hash)
    return [name for name in connected if name not in disabled] or keep


def display_label(output):
    """What the Monitors panel calls an external screen: its model, else its make, else the port."""
    for key in ("model", "make"):
        value = output.get(key)
        if isinstance(value, str) and value.strip() and value.strip().lower() != "unknown":
            return re.sub(r"\s+", " ", value.strip())[:64]
    return output.get("name", "")


def run_with_saved_display_mode(options, run_session, environment):
    """A login asks for one screen. It runs as the extended session, where the Monitors panel's
    choices decide which displays are on (only the main one unless "Use all displays" or a
    display was switched on) and can change while logged in. If that fails before the desktop
    is up, fall back to one screen in a fresh evidence directory rather than returning to the
    login prompt."""
    if not options.single_output:
        return run_session(options)
    extended = argparse.Namespace(**vars(options))
    extended.single_output, extended.experimental_extended = False, True
    extended.display_settings = True
    try:
        result = run_session(extended)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("Platinum Reality session: all displays could not start: " + str(error), file=sys.stderr)
        result = None
    # An authenticated choice of another session intentionally creates no desktop.
    # Preserve its routing code so login-session.py can start KDE after cleanup;
    # a second Log In here would consume the choice as an extended-start failure.
    if result == KDE_SESSION_EXIT:
        return result
    try:
        reached = json.loads((Path(options.evidence) / "session.json").read_text()).get("was_ready", False)
    except (OSError, ValueError):
        reached = False
    if reached:
        return 1 if result is None else result
    fallback = argparse.Namespace(**vars(options))
    fallback.evidence = Path(str(options.evidence) + "-one-display")
    return run_session(fallback)


def describe_displays(outputs, preferred):
    """Version-1 topology payload. Input is this supervisor's own Sway reply.

    Keep host logical coordinates/rotation; the desktop normalizes the origin.
    This function does not change any output configuration.
    """
    if not isinstance(outputs, list) or any(not isinstance(o, dict) for o in outputs):
        raise ValueError("Invalid display inventory")
    active = [o for o in outputs if o.get("active") is True and not o.get("non_desktop", False)]
    if not 1 <= len(active) <= 16:
        raise ValueError("No usable active displays, or too many displays")
    transforms = {"normal", "90", "180", "270", "flipped", "flipped-90", "flipped-180", "flipped-270"}
    records, names = [], set()
    for output in active:
        name, rect = output.get("name", ""), output.get("rect", {})
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name) or name in names:
            raise ValueError("Invalid or duplicate display connector")
        names.add(name)
        if not isinstance(rect, dict) or any(type(rect.get(k)) is not int for k in ("x", "y", "width", "height")):
            raise ValueError("Invalid logical display coordinates")
        if (not -16384 <= rect["x"] <= 16384 or not -16384 <= rect["y"] <= 16384 or
                not 80 <= rect["width"] <= 16384 or not 80 <= rect["height"] <= 16384):
            raise ValueError("Logical display geometry is out of bounds")
        scale, transform = output.get("scale"), output.get("transform")
        if (type(scale) not in (int, float) or not math.isfinite(scale) or not 0.5 <= scale <= 4
                or not isinstance(transform, str) or transform not in transforms):
            raise ValueError("Invalid display scale or transform")
        for prior in records:
            if (rect["x"] < prior["x"]+prior["width"] and prior["x"] < rect["x"]+rect["width"] and
                    rect["y"] < prior["y"]+prior["height"] and prior["y"] < rect["y"]+rect["height"]):
                raise ValueError("Display rectangles overlap")
        records.append({"name": name, **{k: rect[k] for k in ("x", "y", "width", "height")},
                        "scale": scale, "transform": transform})
    width = max(o["x"]+o["width"] for o in records)-min(o["x"] for o in records)
    height = max(o["y"]+o["height"] for o in records)-min(o["y"] for o in records)
    if width > 16384 or height > 16384 or width*height > 64*1024*1024:
        raise ValueError("Display canvas is too large")
    primary, _ = choose_primary_output(active, preferred)
    return {"mode": "extend", "primary": primary["name"], "outputs": sorted(records, key=lambda o: o["name"])}


def publish_displays(path, description, session_id, generation):
    """Publish atomically in a private runtime; never overwrite a live inode."""
    snapshot = {"version": 1, "session": session_id, "generation": generation, **description}
    data = (json.dumps(snapshot, sort_keys=True) + "\n").encode()
    if len(data) > 65536:
        raise ValueError("Display snapshot is too large")
    descriptor, temporary = tempfile.mkstemp(prefix="display-topology.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def display_acknowledged(path, session_id, generation):
    try:
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077 or info.st_size > 1024:
            return False
        reply = json.loads(path.read_text())
        return (isinstance(reply, dict) and type(reply.get("version")) is int and reply["version"] == 1 and reply.get("session") == session_id
                and type(reply.get("generation")) is int and reply["generation"] == generation)
    except (OSError, ValueError):
        return False


def receive_exact(peer, size):
    result = bytearray()
    while len(result) < size:
        part = peer.recv(size - len(result))
        if not part:
            raise ConnectionError("Backend IPC disconnected")
        result.extend(part)
    return bytes(result)


def receive_ipc(peer):
    magic, length, kind = HEADER.unpack(receive_exact(peer, HEADER.size))
    if magic != b"i3-ipc" or length > MAX_IPC:
        raise ValueError("Invalid backend IPC response")
    return kind, json.loads(receive_exact(peer, length))


def ipc(path, kind, payload="", *, timeout=2):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(timeout)
        peer.connect(str(path))
        encoded = payload.encode()
        peer.sendall(HEADER.pack(b"i3-ipc", len(encoded), kind) + encoded)
        returned, response = receive_ipc(peer)
        if returned != kind:
            raise ValueError("Unexpected backend IPC response")
        return response


def wait_for_backend_inventory(path):
    # Sway publishes its IPC socket before the first inventory reply can be
    # ready on a software-rendered VM. This is a startup read, not a command
    # retry. Keep every subsequent query/command on ipc()'s normal 2 s budget.
    return ipc(path, 3, timeout=8)


def nodes(tree):
    yield tree
    for key in ("nodes", "floating_nodes"):
        for child in tree.get(key, []):
            yield from nodes(child)


CURSOR_THEME = "Platinum-MacOS9"


def personal_cursors(environment, home=None):
    """The user's own Mac OS 9 cursors (scripts/import-macos9-assets.py), when imported, for Sway,
    the desktop and every application: XCURSOR_THEME plus a search path that still reaches the
    theme it inherits (the arrow lived in ROM, so the user's previous arrow stays). Never bundled."""
    home = Path(home or environment.get("HOME") or Path.home())
    data = Path(environment.get("XDG_DATA_HOME") or home / ".local/share")
    folder = data / "platinum-reality/assets/cursors"
    theme = folder / CURSOR_THEME
    if not data.is_absolute() or theme.is_symlink() or not (theme / "index.theme").is_file():
        return False
    search = [p for p in environment.get("XCURSOR_PATH", "").split(":") if p] or [
        str(home / ".local/share/icons"), str(home / ".icons"), "/usr/share/icons", "/usr/share/pixmaps"]
    environment["XCURSOR_PATH"] = ":".join([str(folder)] + [p for p in search if p != str(folder)])
    environment["XCURSOR_THEME"] = CURSOR_THEME
    return True


CHIME_NAMES = ("startup-chime.wav", "startup-chime.aiff", "startup-chime.aif", "startup-chime.flac", "startup-chime.ogg")


# Waits (in the helper, never in the supervisor) until PipeWire has a default output: at the first
# Log In it is still starting, and USB speakers appear later still (the first chime was silent,
# 2026-10-22). At most 15 s, then half a second to settle; $1 may be empty (no wpctl: no wait).
CHIME_HELPER = ('i=0; if [ -n "$1" ]; then while [ "$i" -lt 75 ] && ! "$1" inspect @DEFAULT_AUDIO_SINK@ '
                '>/dev/null 2>&1; do sleep 0.2; i=$((i + 1)); done; sleep 0.5; fi; exec "$2" "$3" "$4"')
# Half volume unless Sound ▸ Startup says otherwise: the chime at full volume startled psn_tch
# (2026-10-23). pw-play takes a factor, paplay a value out of 65536.
CHIME_VOLUME = 0.5


def chime_volume_option(player, volume=CHIME_VOLUME):
    return (f"--volume={int(65536 * volume)}" if Path(player).name == "paplay"
            else f"--volume={volume:g}")


def startup_sound_settings(profile):
    """Sound ▸ Startup's choice, <profile>/startup-sound.json: (enabled, volume factor 0–1). Small,
    regular, user-owned only; anything else (missing, linked, malformed) keeps the defaults."""
    settings = {"enabled": True, "volume": CHIME_VOLUME}
    try:
        fd = os.open(Path(profile) / "startup-sound.json", os.O_RDONLY | os.O_CLOEXEC |
                     os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_size > 4096:
                return settings
            data = json.loads(stream.read(4097))
    except (OSError, ValueError, UnicodeError):
        return settings
    if not isinstance(data, dict):
        return settings
    if type(data.get("enabled")) is bool:
        settings["enabled"] = data["enabled"]
    if type(data.get("volume")) is int and 0 <= data["volume"] <= 100:
        settings["volume"] = data["volume"] / 100
    return settings


def startup_chime(environment, home=None, boot_id=Path("/proc/sys/kernel/random/boot_id"),
                  players=("/usr/bin/pw-play", "/usr/bin/paplay"), wpctl="/usr/bin/wpctl", profile=None):
    """Once per boot, when the Log In is first on screen: the user's own startup sound,
    <data>/platinum-reality/assets/startup-chime.* (psn_tch chose the Macintosh IIsi chime, 2026-10-22).
    Never bundled: Macs played it from ROM. Sound ▸ Startup can turn it off or set its volume
    (<profile>/startup-sound.json; psn_tch asked for both, 2026-10-29). Returns the player process,
    or None when it is off or silent, there is no sound or player, or it already played since this boot."""
    home = Path(home or environment.get("HOME") or Path.home())
    data = Path(environment.get("XDG_DATA_HOME") or home / ".local/share")
    state = Path(environment.get("XDG_STATE_HOME") or home / ".local/state")
    if not data.is_absolute() or not state.is_absolute():
        return None
    settings = startup_sound_settings(profile or data / "platinum-reality/profile")
    if not settings["enabled"] or settings["volume"] <= 0:
        return None
    folder = data / "platinum-reality/assets"
    sound = next((folder / name for name in CHIME_NAMES
                  if (folder / name).is_file() and not (folder / name).is_symlink()), None)
    player = next((path for path in players if os.access(path, os.X_OK)), None)
    if not sound or not player:
        return None
    try:
        boot = boot_id.read_text().strip()
    except OSError:
        return None
    marker = state / "platinum-reality/startup-chime-boot"
    try:
        if marker.read_text().strip() == boot:
            return None
    except OSError:
        pass
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(boot + "\n")
    waiter = wpctl if wpctl and os.access(wpctl, os.X_OK) else ""
    return subprocess.Popen(["/bin/sh", "-c", CHIME_HELPER, "startup-chime", waiter, player,
                             chime_volume_option(player, settings["volume"]), str(sound)],
                            env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)


def saved_energy_settings(profile):
    """The Energy Saver panel's energy.json: minutes of inactivity before the displays turn off
    and before the computer sleeps; 0 or absent is never (the default, as before this panel).
    Read-only and bounded; anything unexpected is never."""
    path = Path(profile) / "energy.json"
    result = {"displayOff": 0, "sleep": 0}
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 1024:
            return result
        data = json.loads(path.read_text())
    except (OSError, ValueError, UnicodeError):
        return result
    if not isinstance(data, dict) or data.get("version") != 1:
        return result
    for key, low, high in (("displayOff", 1, 240), ("sleep", 5, 480)):
        value = data.get(key)
        if type(value) is int and low <= value <= high:
            result[key] = value
    return result


def idle_command(lock_command, resume_command, energy=None, backend_ipc=None):
    """swayidle for a physical seat. No lock after idle time (psn_tch, 2026-10-20); sleep, Ctrl+Alt+L
    and loginctl lock-session lock. -C /dev/null: never the user's own swayidle config. psn_tch's has
    "before-sleep swaylock", which swayidle -w ran first and waited on until unlock, so this lock
    missed logind's 5 s window, the laptop slept unlocked, and on wake swaylock then Platinum each
    asked to unlock (2026-10-22; very likely the 2026-10-21 freeze too). Resume replaces the
    pre-sleep lock client without unlocking: a living process/old ready flag cannot prove
    that its UI/rendering survived sleep."""
    command = ["swayidle", "-C", "/dev/null", "-w", "before-sleep", lock_command,
               "after-resume", resume_command, "lock", lock_command]
    # Energy Saver: displays off after their idle time (back on with any input), and sleep (which
    # locks first, through before-sleep). Applications that keep the screen awake (a video playing,
    # Appearance's "never turn the screen off") inhibit both through the idle-inhibit protocol.
    energy = energy or {}
    if energy.get("displayOff") and backend_ipc:
        import shlex
        power = shlex.join(["swaymsg", "-s", str(backend_ipc), "output", "*", "power"])
        command += ["timeout", str(energy["displayOff"] * 60), power + " off", "resume", power + " on"]
    if energy.get("sleep"):
        command += ["timeout", str(energy["sleep"] * 60), "systemctl suspend"]
    return command


def stop_owned(process, group=False):
    if process is None or process.poll() is not None:
        return
    try:
        if group:
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.terminate()
    except ProcessLookupError:
        process.wait(timeout=5)
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        if group:
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait(timeout=5)


def read_control(peer):
    payload = bytearray()
    while len(payload) < 128:
        part = peer.recv(128 - len(payload))
        if not part:
            break
        payload.extend(part)
        if b"\n" in payload:
            break
    return bytes(payload)


def control(path, command):
    with socket.socket(socket.AF_UNIX) as peer:
        # A resume can spend five seconds retiring a hung client, then up to
        # five acquiring its replacement. Ordinary control calls stay unchanged.
        peer.settimeout(13 if command == "resume" else 8)
        peer.connect(str(path))
        peer.sendall(command.encode() + b"\n")
        reply = peer.recv(8192)
    result = json.loads(reply)
    print(json.dumps(result))
    return 0 if result.get("ok") else 1


def require_physical_session(environment):
    if environment.get("WAYLAND_DISPLAY") or environment.get("DISPLAY"):
        raise ValueError("A graphical session is already active in this environment. "
                         "Use --nested-check for testing; log out before a physical-seat run.")
    session = environment.get("XDG_SESSION_ID")
    if not session:
        raise ValueError("A local logind login session (XDG_SESSION_ID) is required.")
    result = subprocess.run(["loginctl", "show-session", session, "-p", "Active", "-p", "Remote",
                             "-p", "Seat", "-p", "VTNr"], capture_output=True, text=True, check=True)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    if (values.get("Active") != "yes" or values.get("Remote") != "no"
            or not values.get("Seat") or int(values.get("VTNr", "0")) <= 0):
        raise ValueError("An active, local physical-seat login is required.")


def run(options):
    environment = os.environ.copy()
    if options.nested_check:
        parent = environment.get("WAYLAND_DISPLAY")
        if not parent:
            raise ValueError("--nested-check requires a parent Wayland display.")
        if not os.path.isabs(parent):
            parent = str(Path(environment["XDG_RUNTIME_DIR"]) / parent)
    else:
        require_physical_session(environment)
        parent = None
    for executable in ("sway", "swaylock", "swayidle"):
        if not shutil.which(executable):
            raise ValueError("Required installed tool is missing: " + executable)
    launcher = options.launcher.expanduser().resolve(strict=True)
    if not launcher.is_file() or not os.access(launcher, os.X_OK):
        raise ValueError("Desktop launcher is not executable")
    runtime_parent = Path(environment.get("XDG_RUNTIME_DIR", ""))
    info = runtime_parent.stat()
    if (not runtime_parent.is_absolute() or runtime_parent.is_symlink()
            or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700):
        raise ValueError("An owned, mode-0700 XDG_RUNTIME_DIR is required.")
    evidence = options.evidence.expanduser().absolute()
    evidence.mkdir(parents=True, exist_ok=False, mode=0o700)
    runtime = Path(tempfile.mkdtemp(prefix="platinum-session.", dir=runtime_parent))
    state = {"mode": "nested-check" if parent else "physical-seat", "ready": False,
             "locked": False, "runtime": str(runtime), "supervisor_pid": os.getpid(),
             "control": str(runtime / "control"), "hardware_qualified": False}
    state_file = evidence / "session.json"
    supervisor_log = (evidence / "supervisor.txt").open("a", buffering=1)
    if not parent:
        # On a physical seat stdout/stderr are tty1, invisible under the desktop. A sleep that
        # went unlocked (2026-10-21) left no trace of what the supervisor was doing.
        os.dup2(supervisor_log.fileno(), 1)
        os.dup2(supervisor_log.fileno(), 2)

    def note(message):
        supervisor_log.write(time.strftime("%Y-%m-%dT%H:%M:%S%z") + f" mono={time.monotonic():.3f} {message}\n")

    def record():
        temporary = evidence / "session.json.tmp"
        temporary.write_text(json.dumps(state, indent=2) + "\n")
        temporary.replace(state_file)

    config = runtime / "sway.conf"
    config.write_text(CONFIG)
    environment["XDG_RUNTIME_DIR"] = str(runtime)
    environment["XDG_CURRENT_DESKTOP"] = "PlatinumReality"
    environment["XDG_SESSION_DESKTOP"] = "platinum-reality"
    environment["XDG_SESSION_TYPE"] = "wayland"
    state["personal_cursors"] = personal_cursors(environment)
    for key in ("SWAYSOCK", "I3SOCK", "DISPLAY", "XAUTHORITY", "WAYLAND_SOCKET",
                "WLR_BACKENDS", "WLR_LIBINPUT_NO_DEVICES", "WLR_HEADLESS_OUTPUTS"):
        environment.pop(key, None)
    if parent:
        environment["WAYLAND_DISPLAY"] = parent
        environment["WLR_BACKENDS"] = "wayland"
    else:
        environment.pop("WAYLAND_DISPLAY", None)
    backend = desktop = locker = idle = events = server = None
    stopping = False
    result = 1

    def interrupted(_signal, _frame):
        nonlocal stopping
        stopping = True

    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    with (evidence / "backend.txt").open("wb") as backend_log, \
            (evidence / "desktop.txt").open("wb") as desktop_log, \
            (evidence / "lock.txt").open("wb") as lock_log, \
            (evidence / "idle.txt").open("wb") as idle_log:
        try:
            backend = subprocess.Popen(["sway", "--config", str(config)], env=environment,
                                       stdin=subprocess.DEVNULL, stdout=backend_log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            state["backend_pid"] = backend.pid
            backend_ipc = runtime / f"sway-ipc.{os.getuid()}.{backend.pid}.sock"
            deadline = time.monotonic() + 12
            while not backend_ipc.exists():
                if backend.poll() is not None or stopping or time.monotonic() > deadline:
                    raise RuntimeError("Display backend did not start; see backend.txt")
                time.sleep(0.05)
            wait_for_backend_inventory(backend_ipc)
            displays = [p for p in runtime.glob("wayland-*") if p.is_socket()]
            if len(displays) != 1:
                raise RuntimeError("Expected exactly one private backend Wayland display")
            child_environment = environment.copy()
            child_environment["WAYLAND_DISPLAY"] = str(displays[0])
            child_environment["QT_QPA_PLATFORM"] = "wayland"
            child_environment["PLATINUM_LOGIN_SESSION"] = "1"
            child_environment.pop("PLATINUM_WALLET_LOGIN", None)
            # --nested-check must never activate the user's wallet, even though it tests
            # login-only Bluetooth behavior. Only a physical session grants this allowance.
            if not parent:
                child_environment["PLATINUM_WALLET_LOGIN"] = "1"
            child_environment["PLATINUM_SESSION_CONTROL"] = str(runtime / "control")
            # The display/bus runtime is private, while audio remains the user's
            # existing service. Otherwise clients search the empty private runtime.
            if (runtime_parent / "pipewire-0").is_socket():
                child_environment.setdefault("PIPEWIRE_RUNTIME_DIR", str(runtime_parent))
            if (runtime_parent / "pulse/native").is_socket():
                child_environment.setdefault("PULSE_SERVER", "unix:" + str(runtime_parent / "pulse/native"))
            state["backend_ipc"] = str(backend_ipc)
            state["backend_display"] = str(displays[0])
            kde_outputs = Path(environment.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "kwinoutputconfig.json"

            # Read by output_preferences, which the one-display start below already calls: assigned
            # after it, they crashed the startup-recovery path (NameError, found by Rocky 2026-10-30).
            preferred_primary = options.primary_output
            display_profile = desktop_profile(options.profile, environment)
            # The Keyboard and Mouse & Trackpad panels' choices, before anything is shown.
            apply_input_settings(backend_ipc, display_profile, state)
            main_preference = saved_main_display(display_profile)
            managed = options.experimental_extended and getattr(options, "display_settings", False)
            display_settings = saved_display_settings(display_profile)

            def kde_preferences(outputs):
                return {} if options.no_kde_output_settings else kde_output_preferences(kde_outputs, outputs)

            def output_preferences(outputs):
                preferences = {**panel_defaults(outputs), **kde_preferences(outputs)}
                # A one-display login keeps the scale its deployment asks for, as the
                # single-output path always did, for a screen nothing else describes.
                if (options.output_scale is not None and getattr(options, "display_settings", False)
                        and display_settings["mode"] != "extend"):
                    for output in outputs:
                        name = output.get("name")
                        if output.get("active") is True and isinstance(name, str) and name not in preferences:
                            preferences[name] = {"transform": "normal", "scale": options.output_scale}
                # The Monitors panel's own choices for a monitor come last and win.
                for entry in display_settings.get("displays", []) if managed else []:
                    name = resolve_main_display(outputs, entry["identity"], active_only=False)
                    if name:
                        preferences[name] = {**preferences.get(name, {}),
                                             **{k: entry[k] for k in ("mode", "scale", "transform") if k in entry}}
                if managed and display_settings["mode"] == "mirror":
                    main = mirror_main(outputs)
                    for name in list(preferences):
                        if name != main:
                            preferences[name] = {k: v for k, v in preferences[name].items() if k != "scale"}
                return preferences

            def mirror_main(outputs):
                """The display the others mirror: the saved main display, else the usual choice."""
                lid = lid_closed()
                wanted = wanted_displays(outputs, lid)
                main = resolve_main_display(outputs, main_preference, active_only=False) or preferred_primary
                if main in wanted:
                    return main
                return choose_primary_output([o for o in outputs if o.get("name") in wanted], main)[0]["name"]

            if options.primary_output and not options.experimental_extended:
                outputs = ipc(backend_ipc, 3)
                names = [o["name"] for o in outputs]
                selected, reason = choose_primary_output(outputs, options.primary_output)
                primary = selected["name"]
                # The screen keeps its KDE rotation and scale, else a known panel's default;
                # --output-scale covers unknown screens, and without it sway chooses their scale.
                preference = output_preferences(outputs).get(primary, {})
                output_scale = preference.get("scale", options.output_scale)
                output_transform = preference.get("transform", "normal")
                commands = [f"output {primary} enable",
                            f"output {primary} transform {output_transform}"]
                if output_scale is not None:
                    commands.append(f"output {primary} scale {output_scale}")
                commands.append(f"output {primary} position 0 0")
                if options.single_output:
                    commands.extend(f"output {name} disable" for name in names if name != primary
                                    and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name))
                commands.append(f"focus output {primary}")
                for command in commands:
                    response = ipc(backend_ipc, 0, command)
                    if not response or not all(r.get("success") for r in response):
                        raise RuntimeError("Backend rejected output configuration")
                state["primary_output"] = primary
                state["output_scale"] = output_scale
                state["output_transform"] = output_transform
                state["output_settings_source"] = ("kde" if primary in kde_preferences(outputs) else
                                                   "panel" if preference else "requested")
                state["output_selection_reason"] = reason
                state["requested_primary_output"] = options.primary_output
                if primary != options.primary_output:
                    state["preferred_output_missing"] = options.primary_output
            display_path = runtime / "display-topology.json"
            display_session = secrets.token_hex(16)
            display_generation = 0
            display_description = None
            arranged_for = None
            primary_chosen_here = False
            all_connected = managed or display_settings["mode"] == "extend"
            # A Monitors panel change waiting for "Keep": the settings to return to, and when.
            display_pending = None
            refused_modes = set()

            def wanted_displays(outputs, lid):
                if not managed:
                    return connected_display_names(outputs, lid)
                main = (resolve_main_display(outputs, main_preference, active_only=False)
                        or preferred_primary)
                return chosen_display_names(outputs, lid, display_settings["mode"], display_settings["off"], main)

            def apply_kde_arrangement(outputs):
                """Extend mode: when the set of connected monitors changes, arrange them as KDE did
                for exactly that set (on/off, positions, main display). Applied once per set, so a
                later choice in the Monitors panel is never fought; one screen always stays on."""
                nonlocal arranged_for, preferred_primary
                lid = lid_closed()
                connected = (tuple(sorted(o.get("name", "") for o in outputs if not o.get("non_desktop", False))), lid,
                             tuple(sorted((o.get("name", ""), o.get("active") is True,
                                           (o.get("rect") or {}).get("width"), (o.get("rect") or {}).get("height"),
                                           o.get("scale"), o.get("transform")) for o in outputs
                                          if not o.get("non_desktop", False))) if all_connected else (),
                             json.dumps(display_settings, sort_keys=True) if managed else "")
                if connected == arranged_for:
                    return False
                arrangement = ({} if options.no_kde_output_settings
                               else kde_arrangement(kde_outputs, outputs, lid=lid))
                state["kde_arrangement"] = {name: place for name, place in sorted(arrangement.items())}
                # Where the Monitors panel put each display (Arrange) wins over KDE's positions.
                for entry in display_settings.get("displays", []) if managed else []:
                    name = entry.get("position") and resolve_main_display(outputs, entry["identity"], active_only=False)
                    if name:
                        arrangement[name] = {**arrangement.get(name, {"primary": False}), "enabled": True,
                                             **entry["position"]}
                if all_connected:
                    wanted = wanted_displays(outputs, lid)
                    # Enable before sizing/placing, and before disabling a shut panel.
                    # Every request goes only to this supervisor's own backend.
                    previous = outputs
                    try:
                        for name in wanted:
                            current = next(o for o in outputs if o.get("name") == name)
                            if not current.get("active"):
                                reply = ipc(backend_ipc, 0, f"output {name} enable")
                                if not reply or not all(r.get("success") for r in reply):
                                    raise ValueError("A connected display could not be enabled")
                        outputs = ipc(backend_ipc, 3)
                        if apply_output_preferences(outputs):
                            outputs = ipc(backend_ipc, 3)
                        if managed and display_settings["mode"] == "mirror":
                            main = mirror_main(outputs)
                            plan = mirror_plan(outputs, main)
                            for name, place in plan.items():
                                current = next(o for o in outputs if o.get("name") == name)
                                if abs(float(current.get("scale") or 0) - place["scale"]) > 0.001:
                                    reply = ipc(backend_ipc, 0, f"output {name} scale {place['scale']}")
                                    if not reply or not all(r.get("success") for r in reply):
                                        raise ValueError("A mirrored display's size was refused")
                            arrangement = {main: {"enabled": True, "x": 0, "y": 0, "primary": True},
                                           **{name: {"enabled": True, "x": place["x"], "y": place["y"], "primary": False}
                                              for name, place in plan.items()}}
                        else:
                            arrangement = extended_connected_arrangement(outputs, arrangement, preferred_primary, lid, wanted)
                    except (ValueError, OSError) as error:
                        # Reject unusable seed/canvas geometry without stranding the desktop
                        # on the newly activated output. Retry only after an inventory/lid
                        # change, rather than fighting the same bad setup every 250 ms.
                        arranged_for = connected
                        state["display_configuration_error"] = str(error)
                        restoration = restore_display_commands(previous)
                        restored = bool(restoration)
                        for command in restoration:
                            reply = ipc(backend_ipc, 0, command)
                            if not reply or not all(r.get("success") for r in reply):
                                restored = False
                                note("display: previous layout restore request rejected")
                        note("display: rejected all-connected layout; requested previous usable outputs: " + str(error))
                        if restored:
                            return True  # Publish the restored usable layout, including at startup.
                        raise
                    state.pop("display_configuration_error", None)
                    state["display_arrangement_source"] = "platinum-all-connected"
                commands = []
                for name, place in arrangement.items():
                    current = next(o for o in outputs if o.get("name") == name)
                    if place["enabled"] and not current.get("active"):
                        commands.append(f"output {name} enable")
                    # Always explicit: Sway re-places an output without a configured position
                    # whenever another one moves, even if it already sits where KDE had it.
                    if place["enabled"]:
                        commands.append(f"output {name} position {place['x']} {place['y']}")
                commands += [f"output {name} disable" for name, place in arrangement.items()
                             if not place["enabled"] and next(o for o in outputs if o.get("name") == name).get("active")]
                if all_connected:
                    commands += [f"output {o['name']} disable" for o in outputs
                                 if o.get("active") and o.get("name") not in wanted
                                 and isinstance(o.get("name"), str)
                                 and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o["name"])
                                 and not o.get("non_desktop", False)]
                if arrangement and not primary_chosen_here:
                    preferred_primary = next(name for name, place in arrangement.items() if place["primary"])
                for command in commands:
                    reply = ipc(backend_ipc, 0, command)
                    if not reply or not all(r.get("success") for r in reply):
                        raise ValueError("A display arrangement request was rejected")
                arranged_for = connected
                return bool(commands)

            def apply_output_preferences(outputs):
                """Extend mode: every screen, including ones plugged in later, takes its KDE (or
                known panel) rotation and scale. Only differences are sent, so polling never loops."""
                changed = False
                for name, preference in output_preferences(outputs).items():
                    current = next((o for o in outputs if o.get("name") == name and o.get("active")), None)
                    if current is None or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
                        continue
                    mode = preference.get("mode")
                    if mode:
                        now = display_modes(current)
                        attempt = (name, tuple(mode.values()))
                        # Only a mode the monitor offers, and never retried after Sway refused it.
                        if (mode in now["modes"] and now.get("mode") != mode and attempt not in refused_modes):
                            response = ipc(backend_ipc, 0, mode_command(name, mode))
                            if bool(response) and all(r.get("success") for r in response):
                                changed = True
                            else:
                                refused_modes.add(attempt)
                    for key in ("transform", "scale"):
                        if key not in preference:
                            continue
                        value = preference[key]
                        if (current.get(key) != value if key == "transform"
                                else abs(float(current.get(key) or 0) - value) > 0.001):
                            response = ipc(backend_ipc, 0, f"output {name} {key} {value}")
                            changed = changed or bool(response) and all(r.get("success") for r in response)
                return changed

            def normalize_origin(outputs):
                """Extend mode: keep the enabled screens' top-left corner at 0,0, preserving how they
                sit relative to each other. Sway draws the global-fullscreen desktop from the layout
                origin, so an offset shifts it off-screen and leaves a black strip. KDE keeps one when
                a disabled panel sat to the left (seen 2026-10-22: the only monitor at x=696)."""
                active = [o for o in outputs if o.get("active") is True and not o.get("non_desktop", False)
                          and isinstance(o.get("rect"), dict)
                          and all(type(o["rect"].get(k)) is int for k in ("x", "y"))
                          and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o.get("name", ""))]
                if not active:
                    return False
                left = min(o["rect"]["x"] for o in active)
                top = min(o["rect"]["y"] for o in active)
                if (left, top) == (0, 0):
                    return False
                # Every enabled screen explicitly, for the same reason as apply_kde_arrangement.
                for output in active:
                    ipc(backend_ipc, 0, f"output {output['name']} position "
                                        f"{output['rect']['x'] - left} {output['rect']['y'] - top}")
                state["display_origin_normalized"] = {"x": left, "y": top}
                return True

            def apply_display_settings(settings):
                """Re-arrange for new Monitors choices; True when the desktop took them."""
                nonlocal display_settings, arranged_for
                display_settings, arranged_for = settings, None
                update_displays()
                return not state.get("display_error") and not state.get("display_configuration_error")

            def revert_display_change(reason):
                nonlocal display_pending, arranged_for
                pending, display_pending = display_pending, None
                state["display_change"] = reason
                apply_display_settings(pending["previous"])
                # Values nothing saved had chosen (Sway's own mode or scale) are put back as they were.
                restore = pending.get("restore")
                if restore:
                    name, before = restore
                    current = next((o for o in ipc(backend_ipc, 3) if o.get("name") == name and o.get("active")), None)
                    if current is not None:
                        commands = []
                        if before.get("mode") and display_modes(current).get("mode") != before["mode"]:
                            commands.append(mode_command(name, before["mode"]))
                        if current.get("transform") != before["transform"]:
                            commands.append(f"output {name} transform {before['transform']}")
                        if abs(float(current.get("scale") or 0) - before["scale"]) > 0.001:
                            commands.append(f"output {name} scale {before['scale']}")
                        for command in commands:
                            ipc(backend_ipc, 0, command)
                        if commands:
                            arranged_for = None
                            update_displays()
                outputs_before = pending.get("outputs_before")
                if outputs_before:
                    current = {o["name"]: o for o in ipc(backend_ipc, 3) if o.get("active") is True}
                    for name, before in outputs_before.items():
                        if name in current and abs(float(current[name].get("scale") or 0) - before["scale"]) > 0.001:
                            ipc(backend_ipc, 0, f"output {name} scale {before['scale']}")
                    positions = {n: (b["x"], b["y"]) for n, b in outputs_before.items()}
                else:
                    positions = pending.get("positions")
                if positions:
                    active = {o["name"] for o in ipc(backend_ipc, 3) if o.get("active") is True}
                    for name, (x, y) in positions.items():
                        if name in active:
                            ipc(backend_ipc, 0, f"output {name} position {x} {y}")
                    arranged_for = None
                    update_displays()
                note("display: " + reason)
                record()

            def request_display_mirror(on):
                """Mirror displays: every display that is on shows the main display (mirror_plan), or
                each shows its own part of the desktop again. Keep or revert, like every change."""
                nonlocal display_pending
                if display_pending is not None:
                    return False
                outputs = ipc(backend_ipc, 3)
                active = [o for o in outputs if o.get("active") is True and not o.get("non_desktop", False)
                          and isinstance(o.get("rect"), dict) and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o.get("name", ""))]
                if (display_settings["mode"] == "mirror") == on:
                    return True
                if on and len(active) < 2:
                    return False
                before = {o["name"]: {"x": o["rect"]["x"], "y": o["rect"]["y"], "scale": float(o.get("scale") or 1)}
                          for o in active}
                displays = list(display_settings.get("displays", []))
                if on:
                    # Each display's own size comes back when mirroring ends.
                    for output in active:
                        entry = next((dict(d) for d in displays
                                      if resolve_main_display(outputs, d["identity"], active_only=False) == output["name"]), {})
                        if "scale" not in entry:
                            displays = [d for d in displays
                                        if resolve_main_display(outputs, d["identity"], active_only=False) != output["name"]]
                            entry.update(identity=main_display_identity(output), scale=float(output.get("scale") or 1))
                            displays.append(entry)
                previous = display_settings
                display_pending = {"previous": previous, "deadline": time.monotonic() + 15, "outputs_before": before}
                if not apply_display_settings({**display_settings, "mode": "mirror" if on else "extend",
                                               "displays": displays[-16:]}):
                    revert_display_change("refused; reverted")
                    return False
                now = [o for o in ipc(backend_ipc, 3) if o.get("active") is True and not o.get("non_desktop", False)]
                if on:
                    main = next((o for o in now if o.get("name") == state.get("primary_output")), None)
                    contains = main is not None and len(now) >= 2 and all(
                        o["rect"]["x"] <= main["rect"]["x"] and o["rect"]["y"] <= main["rect"]["y"]
                        and o["rect"]["x"] + o["rect"]["width"] >= main["rect"]["x"] + main["rect"]["width"]
                        and o["rect"]["y"] + o["rect"]["height"] >= main["rect"]["y"] + main["rect"]["height"]
                        for o in now)
                    took = contains
                else:
                    try:
                        describe_displays(now, None)
                        took = True
                    except ValueError:
                        took = False
                if not took:
                    revert_display_change("refused; reverted")
                    return False
                state["display_change"] = "waiting"
                update_displays()
                return True

            def request_display_position(name, position):
                """Arrange: a display dragged to a new place beside the others. Every display that is on
                keeps its place in the saved arrangement, so they are remembered together."""
                nonlocal display_pending
                if display_pending is not None or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
                    return False
                outputs = ipc(backend_ipc, 3)
                active = [o for o in outputs if o.get("active") is True and not o.get("non_desktop", False)
                          and isinstance(o.get("rect"), dict) and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o.get("name", ""))]
                if name not in {o["name"] for o in active} or len(active) < 2:
                    return False
                before = {o["name"]: (o["rect"]["x"], o["rect"]["y"]) for o in active}
                moved = [{**o, "rect": {**o["rect"], **(position if o["name"] == name else {})}} for o in active]
                left = min(o["rect"]["x"] for o in moved)
                top = min(o["rect"]["y"] for o in moved)
                moved = [{**o, "rect": {**o["rect"], "x": o["rect"]["x"] - left, "y": o["rect"]["y"] - top}} for o in moved]
                describe_displays(moved, None)  # No overlap, bounded canvas; raises otherwise.
                displays = list(display_settings.get("displays", []))
                for output in moved:
                    entry = next((dict(d) for d in displays
                                  if resolve_main_display(outputs, d["identity"], active_only=False) == output["name"]), {})
                    displays = [d for d in displays
                                if resolve_main_display(outputs, d["identity"], active_only=False) != output["name"]]
                    entry.update(identity=main_display_identity(output),
                                 position={"x": output["rect"]["x"], "y": output["rect"]["y"]})
                    displays.append(entry)
                previous = display_settings
                display_pending = {"previous": previous, "deadline": time.monotonic() + 15, "positions": before}
                if not apply_display_settings({**display_settings, "displays": displays[-16:]}):
                    revert_display_change("refused; reverted")
                    return False
                placed = {o["name"]: (o["rect"]["x"], o["rect"]["y"]) for o in ipc(backend_ipc, 3) if o.get("active") is True}
                wanted = {o["name"]: (o["rect"]["x"], o["rect"]["y"]) for o in moved}
                if placed != wanted:
                    revert_display_change("refused; reverted")
                    return False
                state["display_change"] = "waiting"
                update_displays()
                return True

            def request_display_setting(name, key, value):
                """A Monitors panel resolution, size or rotation for a display that is on: applied at
                once, kept only after "Keep" (else reverted after 15 seconds), like On and Off."""
                nonlocal display_pending
                if display_pending is not None or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
                    return False
                outputs = ipc(backend_ipc, 3)
                output = next((o for o in outputs if o.get("name") == name and o.get("active") is True
                               and not o.get("non_desktop", False)), None)
                if output is None:
                    return False
                modes = display_modes(output)
                if ((key == "mode" and value not in modes["modes"]) or
                        (key == "scale" and not 0.5 <= value <= 4) or
                        (key == "transform" and value not in DISPLAY_ROTATIONS)):
                    return False
                before = {"mode": modes.get("mode"), "scale": float(output.get("scale") or 1),
                          "transform": output.get("transform")}
                if before["transform"] not in ("normal", "90", "180", "270", "flipped", "flipped-90",
                                               "flipped-180", "flipped-270"):
                    return False
                identity = main_display_identity(output)
                others = [d for d in display_settings.get("displays", [])
                          if resolve_main_display(outputs, d["identity"], active_only=False) != name]
                entry = next((dict(d) for d in display_settings.get("displays", [])
                              if resolve_main_display(outputs, d["identity"], active_only=False) == name), {})
                entry.update(identity=identity, **{key: value})
                previous = display_settings
                if not apply_display_settings({**display_settings, "displays": (others + [entry])[-16:]}):
                    display_pending = {"previous": previous, "restore": (name, before)}
                    revert_display_change("refused; reverted")
                    return False
                after = next((o for o in ipc(backend_ipc, 3) if o.get("name") == name), {})
                took = (display_modes(after).get("mode") == value if key == "mode" else
                        abs(float(after.get("scale") or 0) - value) <= 0.001 if key == "scale" else
                        after.get("transform") == value)
                display_pending = {"previous": previous, "deadline": time.monotonic() + 15,
                                   "restore": (name, before)}
                if not took:
                    revert_display_change("refused; reverted")
                    return False
                state["display_change"] = "waiting"
                update_displays()  # Publish "pending" for the panel's Keep dialog.
                return True

            def request_display_switch(name, on):
                """A Monitors panel "On" box. Applied at once, kept only after "Keep" (else
                reverted after 15 seconds); one display always stays on."""
                nonlocal display_pending
                if display_pending is not None or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
                    return False
                outputs = ipc(backend_ipc, 3)
                lid = lid_closed()
                if name not in connected_display_names(outputs, lid):
                    return False
                output = next(o for o in outputs if o.get("name") == name)
                active = {o["name"] for o in outputs if o.get("active") is True and not o.get("non_desktop", False)}
                if (name in active) == on:
                    return True
                identity = main_display_identity(output)
                off = [i for i in display_settings["off"]
                       if resolve_main_display(outputs, i, active_only=False) != name]
                if on:
                    if display_settings["mode"] != "extend":
                        # From one display to several: everything else stays as it is now.
                        off += [main_display_identity(o) for o in outputs
                                if o.get("name") in connected_display_names(outputs, lid)
                                and o["name"] not in active and o["name"] != name]
                else:
                    if len(active) < 2:
                        return False
                    off.append(identity)
                previous = display_settings
                mode = display_settings["mode"] if display_settings["mode"] in ("extend", "mirror") else "extend"
                if not apply_display_settings({**display_settings, "mode": mode, "off": off[:16]}):
                    apply_display_settings(previous)
                    return False
                now_active = {o["name"] for o in ipc(backend_ipc, 3) if o.get("active") is True}
                if (name in now_active) != on:
                    apply_display_settings(previous)
                    return False
                display_pending = {"previous": previous, "deadline": time.monotonic() + 15}
                state["display_change"] = "waiting"
                update_displays()  # Publish "pending" for the panel's Keep dialog.
                return True

            def keep_display_change():
                nonlocal display_pending
                if display_pending is None:
                    return False
                try:
                    save_display_settings(display_profile, display_settings["mode"], display_settings["off"],
                                          display_settings.get("displays", []))
                except OSError as error:
                    state["display_save_error"] = str(error)
                    revert_display_change("not saved; reverted")
                    return False
                display_pending = None
                state["display_change"] = "kept"
                state.pop("display_save_error", None)
                update_displays()
                record()
                return True

            def update_displays():
                nonlocal display_generation, display_description
                try:
                    outputs = ipc(backend_ipc, 3)
                    if apply_kde_arrangement(outputs):
                        outputs = ipc(backend_ipc, 3)
                    if apply_output_preferences(outputs):
                        outputs = ipc(backend_ipc, 3)
                    if normalize_origin(outputs):
                        outputs = ipc(backend_ipc, 3)
                    restored_primary = resolve_main_display(outputs, main_preference)
                    state["main_display_preference"] = ("matched" if restored_primary else
                                                        "unavailable" if main_preference else "unset")
                    mirroring = managed and display_settings["mode"] == "mirror"
                    if mirroring:
                        main = mirror_main(outputs)
                        active = [o for o in outputs if o.get("active") is True and not o.get("non_desktop", False)]
                        description = describe_displays([o for o in active if o.get("name") == main], main)
                        right = max(o["rect"]["x"] + o["rect"]["width"] for o in active)
                        bottom = max(o["rect"]["y"] + o["rect"]["height"] for o in active)
                        if min(min(o["rect"]["x"], o["rect"]["y"]) for o in active) < 0 or right > 16384 or bottom > 16384:
                            raise ValueError("Mirrored display canvas is out of bounds")
                        description["canvas"] = {"width": right, "height": bottom}
                        description["mirrors"] = [{"name": o["name"], "label": display_label(o)}
                                                  for o in sorted(active, key=lambda o: o["name"]) if o["name"] != main]
                    else:
                        description = describe_displays(outputs, restored_primary or preferred_primary)
                    if managed:
                        description["labels"] = {o["name"]: display_label(o) for o in outputs
                                                 if o.get("name") in {d["name"] for d in description["outputs"]}}
                        lid = lid_closed()
                        shown = connected_display_names(outputs, lid)
                        description["available"] = [
                            {"name": o["name"], "label": display_label(o),
                             "lid_closed": o["name"] not in shown}
                            for o in sorted(outputs, key=lambda o: str(o.get("name")))
                            if o.get("active") is not True and not o.get("non_desktop", False)
                            and isinstance(o.get("name"), str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", o["name"])][:16]
                        description["pending"] = display_pending is not None
                        description["modes"] = {o["name"]: display_modes(o) for o in outputs
                                                if o.get("name") in description["labels"]}
                except ValueError as error:
                    # Unplugging every display must not kill applications. Keep
                    # the last valid layout until usable outputs return.
                    message = str(error)
                    if state.get("display_error") != message:
                        state["display_error"] = message
                        record()
                    return
                if description != display_description:
                    if managed:
                        # The one-display path's record of its screen, kept for the main display.
                        main = next(o for o in outputs if o.get("name") == description["primary"])
                        state.update(output_scale=main.get("scale"), output_transform=main.get("transform"),
                                     output_settings_source=("kde" if main["name"] in kde_preferences(outputs) else
                                                             "panel" if main["name"] in panel_defaults(outputs) else
                                                             "requested"),
                                     output_selection_reason=choose_primary_output(outputs, options.primary_output)[1])
                        if options.primary_output and options.primary_output != "auto" and not any(
                                o.get("name") == options.primary_output for o in outputs):
                            state["preferred_output_missing"] = options.primary_output
                        else:
                            # A screen that was late at startup (a dock) is no longer missing.
                            state.pop("preferred_output_missing", None)
                    display_generation += 1
                    publish_displays(display_path, description, display_session, display_generation)
                    display_description = description
                    state.update(display_topology=str(display_path), display_generation=display_generation,
                                 primary_output=description["primary"], display_mode="extend")
                    state.pop("display_error", None)
                    record()
                elif "display_error" in state:
                    state.pop("display_error")
                    record()
                if display_acknowledged(Path(str(display_path)+".applied"), display_session, display_generation):
                    if state.get("display_applied_generation") != display_generation:
                        state["display_applied_generation"] = display_generation
                        record()

            if options.experimental_extended:
                update_displays()
                if not display_generation:
                    raise RuntimeError("No usable initial extended display layout")
                state["requested_primary_output"] = preferred_primary or ""

            def login_gate():
                """--start-locked (Log In replacing the tty1 text login): authenticate before the desktop
                exists, then return the chosen session, "platinum" or "kde". If the Platinum Log In cannot
                run, swaylock authenticates instead and Platinum Reality starts. If neither succeeds, the
                session ends (back to the login service). Never unauthenticated."""
                nonlocal locker
                platinum = launcher.parent.parent / "lib/platinum-reality/platinum-lock"
                ready_r, ready_w = os.pipe()
                choice_r, choice_w = os.pipe()
                try:
                    command = [str(platinum), "--login", "--default-session", options.default_session,
                               "--ready-fd", str(ready_w), "--session-fd", str(choice_w)]
                    if options.profile:
                        command += ["--profile", str(options.profile.expanduser().absolute())]
                    if platinum.is_file() and os.access(platinum, os.X_OK):
                        locker = subprocess.Popen(command, env=child_environment, pass_fds=(ready_w, choice_w),
                                                  stdin=subprocess.DEVNULL, stdout=lock_log,
                                                  stderr=subprocess.STDOUT, start_new_session=True)
                        state.update(locked=True, locker="platinum-login", lock_pid=locker.pid)
                        record()
                        note(f"login: started Platinum Log In pid {locker.pid}")
                    os.close(ready_w)
                    os.close(choice_w)
                    ready_w = choice_w = None
                    shown = False
                    while locker and locker.poll() is None:
                        if backend.poll() is not None:
                            raise RuntimeError("Backend ended during Log In")
                        # The Log In writes a newline once it is on every output: the startup chime.
                        if not shown and select.select([ready_r], [], [], 0)[0]:
                            shown = True
                            if os.read(ready_r, 16):
                                try:
                                    if startup_chime(child_environment,
                                                     profile=desktop_profile(options.profile, environment)):
                                        note("login: startup chime")
                                except OSError as error:
                                    note(f"login: startup chime failed: {error}")
                        if options.experimental_extended:
                            update_displays()
                        time.sleep(0.25)
                    if locker and locker.returncode == 0:
                        chosen = os.read(choice_r, 16).decode("ascii", "replace").strip()
                        note(f"login: authenticated; session {chosen or '(none reported)'}")
                        return chosen if chosen in ("platinum", "kde") else "platinum"
                    note(f"login: Platinum Log In unavailable or exited "
                         f"{locker.returncode if locker else 'missing'}; swaylock authenticates instead")
                finally:
                    for descriptor in (ready_r, ready_w, choice_r, choice_w):
                        if descriptor is not None:
                            os.close(descriptor)
                locker = subprocess.Popen(["swaylock", "--config", "/dev/null", "--color", "bbbbbb"],
                                          env=child_environment, stdin=subprocess.DEVNULL, stdout=lock_log,
                                          stderr=subprocess.STDOUT, start_new_session=True)
                state.update(locker="swaylock-login", lock_pid=locker.pid)
                record()
                while locker.poll() is None:
                    if backend.poll() is not None:
                        raise RuntimeError("Backend ended during Log In")
                    time.sleep(0.25)
                if locker.returncode != 0:
                    raise RuntimeError("Log In failed; ending the session")
                note("login: authenticated by swaylock; session platinum")
                return "platinum"

            if options.start_locked:
                chosen = login_gate()
                state.update(locked=False, login_session=chosen)
                record()
                if chosen == "kde":
                    # login-session.py starts the KDE fallback after this supervisor has cleaned up.
                    return KDE_SESSION_EXIT
            events = socket.socket(socket.AF_UNIX)
            events.settimeout(2)
            events.connect(str(backend_ipc))
            payload = b'["binding"]'
            events.sendall(HEADER.pack(b"i3-ipc", len(payload), 2) + payload)
            _, subscribed = receive_ipc(events)
            if not subscribed.get("success"):
                raise RuntimeError("Backend keybinding subscription failed")
            server = socket.socket(socket.AF_UNIX)
            server.bind(str(runtime / "control"))
            server.listen(4)
            arguments = [str(launcher)]
            for name in ("workspace", "profile"):
                if getattr(options, name):
                    arguments.extend(["--" + name, str(getattr(options, name).expanduser().absolute())])
            arguments.extend(options.shell_argument)
            if options.experimental_extended:
                arguments.extend(["--display-topology", str(display_path)])
            desktop = subprocess.Popen(arguments, env=child_environment, stdin=subprocess.DEVNULL,
                                       stdout=desktop_log, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            state["launcher_pid"] = desktop.pid
            record()

            next_lock_retry = 0
            # The Platinum Log In lock ships with the release; swaylock remains the fallback.
            platinum_lock = launcher.parent.parent / "lib/platinum-reality/platinum-lock"

            def locker_command(writer):
                if (options.locker == "platinum" and platinum_lock.is_file()
                        and os.access(platinum_lock, os.X_OK) and state.get("platinum_lock_failures", 0) < 2):
                    command = [str(platinum_lock), "--ready-fd", str(writer)]
                    if options.profile:
                        command += ["--profile", str(options.profile.expanduser().absolute())]
                    return "platinum", command
                return "swaylock", ["swaylock", "--config", "/dev/null", "--color", "bbbbbb",
                                    "--ready-fd", str(writer)]

            def platinum_lock_failed():
                if state.get("locker") == "platinum":
                    state["platinum_lock_failures"] = state.get("platinum_lock_failures", 0) + 1

            def lock(refresh=False):
                nonlocal locker, next_lock_retry
                if not refresh and locker and locker.poll() is None and state.get("lock_ready"):
                    return True
                # A failed/abandoned lock is not an authentication result. Keep
                # logout guarded while reacquiring the compositor's lock, even
                # when an attempt times out before its ready notification.
                state.update(locked=True, lock_ready=False)
                if refresh:
                    # ext-session-lock stays held when its client disconnects. Retire the
                    # pre-sleep UI (including a hung render thread), then take over the
                    # abandoned lock. Never send unlock_and_destroy or accept its exit as
                    # authentication. Logout stays guarded even if reacquisition fails.
                    state["lock_refreshes"] = state.get("lock_refreshes", 0) + 1
                    note(f"lock: refreshing client pid {locker.pid if locker else 'none'}")
                    record()
                    stop_owned(locker)
                reader, writer = os.pipe()
                started = time.monotonic()
                try:
                    state["locker"], command = locker_command(writer)
                    locker = subprocess.Popen(command,
                                              env=child_environment, pass_fds=(writer,),
                                              stdin=subprocess.DEVNULL, stdout=lock_log,
                                              stderr=subprocess.STDOUT, start_new_session=True)
                    state["lock_pid"] = locker.pid
                    note(f"lock: started {state['locker']} pid {locker.pid}")
                    os.close(writer)
                    writer = None
                    readable, _, _ = select.select([reader], [], [], 5)
                    state["lock_ready"] = bool(readable and os.read(reader, 1) == b"\n")
                    if not state["lock_ready"]:
                        stop_owned(locker)
                        platinum_lock_failed()
                        state["lock_error"] = "Locker did not secure the display; retrying"
                    else:
                        state.pop("lock_error", None)
                    note(f"lock: ready={state['lock_ready']} after {time.monotonic() - started:.3f}s")
                    record()
                    return state["lock_ready"]
                except OSError as error:
                    state["lock_error"] = "Cannot start locker: " + str(error)
                    note("lock: " + state["lock_error"])
                    record()
                    return False
                finally:
                    next_lock_retry = time.monotonic() + 2
                    os.close(reader)
                    if writer is not None:
                        os.close(writer)

            def reconcile_lock():
                nonlocal next_lock_retry
                if not state["locked"]:
                    return
                if locker and locker.poll() is not None and state.get("lock_ready"):
                    state["lock_exit"] = locker.returncode
                    if locker.returncode == 0:
                        state.update(locked=False, lock_ready=False)
                        state.pop("lock_error", None)
                        state.pop("platinum_lock_failures", None)
                        record()
                        return
                    platinum_lock_failed()
                    state.update(lock_ready=False,
                                 lock_recoveries=state.get("lock_recoveries", 0) + 1,
                                 lock_error="Locker exited unexpectedly; recovering without unlocking")
                    next_lock_retry = 0
                    record()
                if not state.get("lock_ready") and time.monotonic() >= next_lock_retry:
                    lock()

            # Never listen for the host's sleep/lock events in a nested check.
            if not parent:
                import shlex
                lock_command = shlex.join([sys.executable, str(Path(__file__).resolve()),
                                           "--control", str(runtime / "control"), "lock"])
                resume_command = shlex.join([sys.executable, str(Path(__file__).resolve()),
                                             "--control", str(runtime / "control"), "resume"])
                def start_idle():
                    return subprocess.Popen(idle_command(lock_command, resume_command,
                                                         saved_energy_settings(display_profile), backend_ipc),
                                            env=child_environment, stdin=subprocess.DEVNULL,
                                            stdout=idle_log, stderr=subprocess.STDOUT,
                                            start_new_session=True)
                idle = start_idle()
            deadline = time.monotonic() + 15
            next_display_poll = 0
            while not stopping and backend.poll() is None and desktop.poll() is None:
                inspect_displays = options.experimental_extended and time.monotonic() >= next_display_poll
                if inspect_displays:
                    if display_pending is not None and time.monotonic() >= display_pending["deadline"]:
                        revert_display_change("not kept; reverted")
                    update_displays()
                    next_display_poll = time.monotonic()+0.25
                reconcile_lock()
                if idle and idle.poll() is not None:
                    raise RuntimeError("Idle/lock monitor stopped; see idle.txt")
                if not state["ready"] or inspect_displays:
                    tree = ipc(backend_ipc, 4)
                    windows = [n for n in nodes(tree) if n.get("app_id") and n.get("visible")]
                    if options.experimental_extended:
                        for window in windows:
                            if window.get("fullscreen_mode") != 2:
                                identity = window.get("id")
                                if type(identity) is not int or identity <= 0:
                                    raise RuntimeError("Invalid owned desktop container")
                                reply = ipc(backend_ipc, 0, f"[con_id={identity}] fullscreen enable global")
                                if not reply or not all(r.get("success") for r in reply):
                                    raise RuntimeError("Backend rejected extended desktop span")
                    applied = not options.experimental_extended or state.get("display_applied_generation") == display_generation
                    if not state["ready"] and windows and any(n.get("fullscreen_mode") for n in windows) and applied:
                        state["ready"] = True
                        state["was_ready"] = True  # Kept after logout, unlike "ready".
                        state["outer_window_pids"] = [n.get("pid") for n in windows]
                        record()
                    elif not state["ready"] and time.monotonic() > deadline:
                        raise RuntimeError("Desktop did not map full screen; see desktop.txt")
                readable, _, _ = select.select([server, events], [], [], 0.1)
                for peer in readable:
                    if peer is events:
                        _, event = receive_ipc(events)
                        if event.get("binding", {}).get("command") == "nop platinum-lock":
                            note("lock: Ctrl+Alt+L")
                            # Also a recovery key for an unresponsive lock UI; a second
                            # invocation must never require terminating the whole desktop.
                            lock(refresh=state["locked"])
                    else:
                        connection, _ = server.accept()
                        with connection:
                            connection.settimeout(0.5)
                            try:
                                command = read_control(connection)
                                ok = False
                                if command == b"status\n":
                                    ok = True
                                elif command == b"lock\n":
                                    note("lock: control request")
                                    ok = lock()
                                elif command == b"resume\n":
                                    note("lock: after-resume refresh request")
                                    ok = lock(refresh=True)
                                elif command == b"energy apply\n" and not state["locked"] and idle is not None:
                                    # Energy Saver saved energy.json: restart swayidle with its times.
                                    stop_owned(idle, group=True)
                                    idle = start_idle()
                                    state["energy"] = saved_energy_settings(display_profile)
                                    ok = True
                                    record()
                                elif command == b"input apply\n" and not state["locked"]:
                                    # The desktop saved input.json; this rereads and applies it.
                                    ok = apply_input_settings(backend_ipc, display_profile, state)
                                    record()
                                elif command == b"logout\n" and not state["locked"]:
                                    # Normal xdg close: Save/Cancel remains the app's choice.
                                    response = ipc(backend_ipc, 0, '[shell="xdg_shell"] kill')
                                    ok = bool(response) and all(r.get("success") for r in response)
                                elif (options.experimental_extended and not state["locked"] and
                                      command.startswith(b"primary ") and command.endswith(b"\n")):
                                    requested = command[len(b"primary "):-1].decode("ascii")
                                    connected = {o["name"] for o in display_description["outputs"]}
                                    if re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", requested) and requested in connected:
                                        previous_preference = preferred_primary
                                        previous_identity = main_preference
                                        try:
                                            outputs = ipc(backend_ipc, 3)
                                            chosen = next((o for o in outputs if o.get("name") == requested
                                                           and o.get("active") is True
                                                           and not o.get("non_desktop", False)), None)
                                            if chosen is None:
                                                raise ValueError("The requested main display is no longer active")
                                            main_preference = main_display_identity(chosen)
                                            preferred_primary = requested
                                            update_displays()
                                            if state.get("primary_output") != requested or state.get("display_error"):
                                                raise ValueError("The main display could not be applied")
                                            save_main_display(display_profile, main_preference)
                                            ok = True
                                        except (OSError, ValueError) as error:
                                            state["main_display_save_error"] = str(error)
                                            main_preference = previous_identity
                                            preferred_primary = previous_preference
                                            update_displays()
                                            record()
                                        if ok:
                                            primary_chosen_here = True  # KDE's main display no longer applies.
                                            state["requested_primary_output"] = requested
                                            state.pop("main_display_save_error", None)
                                            record()
                                elif (managed and not state["locked"] and command.startswith(b"display ")
                                      and command.endswith(b"\n")):
                                    words = command[len(b"display "):-1].decode("ascii").split(" ")
                                    if words == ["keep"]:
                                        ok = keep_display_change()
                                    elif words == ["revert"] and display_pending is not None:
                                        revert_display_change("reverted")
                                        ok = True
                                    elif len(words) == 2 and words[0] == "mirror" and words[1] in ("on", "off"):
                                        try:
                                            ok = request_display_mirror(words[1] == "on")
                                        except (OSError, ValueError, StopIteration) as error:
                                            state["display_change"] = "failed: " + str(error)[:200]
                                            ok = False
                                    elif len(words) == 2 and words[1] in ("on", "off"):
                                        try:
                                            ok = request_display_switch(words[0], words[1] == "on")
                                        except (OSError, ValueError, StopIteration) as error:
                                            state["display_change"] = "failed: " + str(error)[:200]
                                            ok = False
                                    elif len(words) == 3 and words[1] == "position":
                                        try:
                                            value = parse_display_setting("position", words[2])
                                            ok = value is not None and request_display_position(words[0], value)
                                        except (OSError, ValueError, StopIteration) as error:
                                            state["display_change"] = "failed: " + str(error)[:200]
                                            ok = False
                                    elif len(words) == 3 and words[1] in ("mode", "scale", "rotate"):
                                        try:
                                            value = parse_display_setting(words[1], words[2])
                                            ok = value is not None and request_display_setting(
                                                words[0], "transform" if words[1] == "rotate" else words[1], value)
                                        except (OSError, ValueError, StopIteration) as error:
                                            state["display_change"] = "failed: " + str(error)[:200]
                                            ok = False
                                    record()
                                connection.sendall(json.dumps({"ok": ok, **state}).encode())
                            except (OSError, ConnectionError, UnicodeError):
                                # An interrupted/malformed control client cannot end the session.
                                pass
            result = desktop.returncode if desktop.poll() is not None else 1
            state["desktop_exit"] = result
        finally:
            # All targets were started by this supervisor. Never contact inherited SWAYSOCK.
            stop_owned(idle, group=True)
            if desktop and desktop.poll() is None and backend and backend.poll() is not None:
                # The Wayland client normally notices loss of the backend itself.
                # Give its launcher time to reap it and clean its private bus first.
                try:
                    desktop.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    pass
            stop_owned(desktop, group=True)
            if backend and backend.poll() is None:
                try:
                    ipc(runtime / f"sway-ipc.{os.getuid()}.{backend.pid}.sock", 0, "exit")
                except (OSError, ValueError, ConnectionError):
                    pass
                try:
                    backend.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    pass
            stop_owned(backend)
            stop_owned(locker)
            if events:
                events.close()
            if server:
                server.close()
            # No recursive deletion: unexpected data or mountpoints are retained.
            for entry in runtime.iterdir():
                mode = entry.lstat().st_mode
                if stat.S_ISSOCK(mode) or (stat.S_ISREG(mode) and entry.name in
                        ("sway.conf", "display-topology.json", "display-topology.json.applied")) or (stat.S_ISREG(mode) and
                        re.fullmatch(r"wayland-[0-9]+\.lock", entry.name)):
                    entry.unlink()
            try:
                runtime.rmdir()
                state["runtime_removed"] = True
            except OSError:
                state["runtime_removed"] = False
                print("Session runtime retained without recursive deletion: " + str(runtime),
                      file=sys.stderr)
                result = 1
            state["finished"] = True
            state["ready"] = False
            record()
    return result


def adopt_previous_name(data):
    """As session_support.adopt_previous_name (this file also runs alone from a release folder):
    <data>/platinum-dreams moves to <data>/platinum-reality once, leaving a link behind."""
    old, new = Path(data) / "platinum-dreams", Path(data) / "platinum-reality"
    if not old.is_dir() or old.is_symlink() or new.exists() or new.is_symlink():
        return False
    try:
        os.rename(old, new)
    except OSError:
        return False
    try:
        old.symlink_to("platinum-reality")
    except OSError:
        pass
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control", type=Path, help="Send lock/resume/status/logout or experimental primary choice to an owned session")
    parser.add_argument("command", nargs="?", choices=("lock", "resume", "status", "logout", "primary"))
    parser.add_argument("connector", nargs="?")
    parser.add_argument("--launcher", type=Path, help="Qualified installed/staged platinum-reality launcher")
    parser.add_argument("--nested-check", action="store_true")
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--shell-argument", action="append", default=[])
    parser.add_argument("--primary-output", help="Prefer a known connected output")
    parser.add_argument("--no-kde-output-settings", action="store_true",
                        help="Do not take each screen's rotation and scale from KDE's kwinoutputconfig.json")
    parser.add_argument("--output-scale", type=float, default=None,
                        help="Scale for a screen with no known preference (default: sway chooses); "
                             "experimental Extend preserves existing backend scaling")
    parser.add_argument("--single-output", action="store_true")
    parser.add_argument("--start-locked", action="store_true",
                        help="Log In before the desktop starts (replacing the tty1 text login); exit "
                             f"{KDE_SESSION_EXIT} when KDE was chosen")
    parser.add_argument("--default-session", choices=("platinum", "kde"), default="platinum",
                        help="With --start-locked: the session the Log In offers first")
    parser.add_argument("--locker", choices=("platinum", "swaylock"), default="platinum",
                        help="Screen lock: the Platinum Log In dialog (swaylock if it fails twice), or swaylock")
    parser.add_argument("--experimental-extended", action="store_true",
                        help="Development only: span current backend outputs and publish topology; does not configure modes")
    parser.add_argument("--display-settings", action="store_true",
                        help="With --experimental-extended: the Monitors panel's saved and live choices decide which displays are on")
    options = parser.parse_args()
    if options.primary_output and not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", options.primary_output):
        parser.error("Invalid output connector name")
    if options.output_scale is not None and not 0.5 <= options.output_scale <= 4:
        parser.error("Output scale must be between 0.5 and 4")
    if options.experimental_extended and options.single_output:
        parser.error("--experimental-extended cannot be combined with --single-output")
    if options.control:
        if not options.command:
            parser.error("--control requires lock, resume, status, logout or primary")
        if options.command == "primary":
            if not options.connector or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", options.connector):
                parser.error("primary requires a valid connector name")
            return control(options.control, "primary " + options.connector)
        if options.connector:
            parser.error("Only primary accepts a connector argument")
        return control(options.control, options.command)
    if options.command or options.connector or not options.launcher or not options.evidence:
        parser.error("--launcher and a new --evidence directory are required")
    if not options.profile and not options.nested_check:  # a real session with the default profile
        adopt_previous_name(Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share"))
    try:
        return run_with_saved_display_mode(options, run, os.environ)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("Platinum Reality session: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
