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
bindsym --inhibited Ctrl+Alt+l nop platinum-lock
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


def saved_display_mode(profile):
    """The Monitors control panel's choice for this login: "extend" (every connected display
    shows the desktop) or "single". Read-only and bounded; anything unexpected is "single"."""
    path = Path(profile) / "displays.json"
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 4096:
            return "single"
        data = json.loads(path.read_text())
    except (OSError, ValueError, UnicodeError):
        return "single"
    return "extend" if isinstance(data, dict) and data.get("mode") == "extend" else "single"


def run_with_saved_display_mode(options, run_session, environment):
    """A login asks for one screen. If the Monitors panel chose every display, run the extended
    session instead; if that fails before the desktop is up, fall back to one screen in a fresh
    evidence directory rather than returning to the login prompt."""
    if not options.single_output or saved_display_mode(desktop_profile(options.profile, environment)) != "extend":
        return run_session(options)
    extended = argparse.Namespace(**vars(options))
    extended.single_output, extended.experimental_extended = False, True
    try:
        result = run_session(extended)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("Platinum Reality session: all displays could not start: " + str(error), file=sys.stderr)
        result = None
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


def ipc(path, kind, payload=""):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(2)
        peer.connect(str(path))
        encoded = payload.encode()
        peer.sendall(HEADER.pack(b"i3-ipc", len(encoded), kind) + encoded)
        returned, response = receive_ipc(peer)
        if returned != kind:
            raise ValueError("Unexpected backend IPC response")
        return response


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
                '>/dev/null 2>&1; do sleep 0.2; i=$((i + 1)); done; sleep 0.5; fi; exec "$2" "$3"')


def startup_chime(environment, home=None, boot_id=Path("/proc/sys/kernel/random/boot_id"),
                  players=("/usr/bin/pw-play", "/usr/bin/paplay"), wpctl="/usr/bin/wpctl"):
    """Once per boot, when the Log In is first on screen: the user's own startup sound,
    <data>/platinum-reality/assets/startup-chime.* (psn_tch chose the Macintosh IIsi chime, 2026-10-22).
    Never bundled: Macs played it from ROM. Returns the player process, or None when there is
    no sound, no player, or it already played since this boot."""
    home = Path(home or environment.get("HOME") or Path.home())
    data = Path(environment.get("XDG_DATA_HOME") or home / ".local/share")
    state = Path(environment.get("XDG_STATE_HOME") or home / ".local/state")
    if not data.is_absolute() or not state.is_absolute():
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
    return subprocess.Popen(["/bin/sh", "-c", CHIME_HELPER, "startup-chime", waiter, player, str(sound)],
                            env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)


def idle_command(lock_command):
    """swayidle for a physical seat. No lock after idle time (psn_tch, 2026-10-20); sleep, Ctrl+Alt+L
    and loginctl lock-session lock. -C /dev/null: never the user's own swayidle config. psn_tch's has
    "before-sleep swaylock", which swayidle -w ran first and waited on until unlock, so this lock
    missed logind's 5 s window, the laptop slept unlocked, and on wake swaylock then Platinum each
    asked to unlock (2026-10-22; very likely the 2026-10-21 freeze too). Locking again after resume
    stays as a safety net; an already-ready lock returns at once."""
    return ["swayidle", "-C", "/dev/null", "-w", "before-sleep", lock_command,
            "after-resume", lock_command, "lock", lock_command]


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
        peer.settimeout(8)
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

            def kde_preferences(outputs):
                return {} if options.no_kde_output_settings else kde_output_preferences(kde_outputs, outputs)

            def output_preferences(outputs):
                return {**panel_defaults(outputs), **kde_preferences(outputs)}

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
            preferred_primary = options.primary_output
            arranged_for = None
            primary_chosen_here = False

            def apply_kde_arrangement(outputs):
                """Extend mode: when the set of connected monitors changes, arrange them as KDE did
                for exactly that set (on/off, positions, main display). Applied once per set, so a
                later choice in the Monitors panel is never fought; one screen always stays on."""
                nonlocal arranged_for, preferred_primary
                connected = tuple(sorted(o.get("name", "") for o in outputs if not o.get("non_desktop", False)))
                if connected == arranged_for:
                    return False
                arranged_for = connected
                arrangement = ({} if options.no_kde_output_settings
                               else kde_arrangement(kde_outputs, outputs, lid=lid_closed()))
                state["kde_arrangement"] = {name: place for name, place in sorted(arrangement.items())}
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
                if arrangement and not primary_chosen_here:
                    preferred_primary = next(name for name, place in arrangement.items() if place["primary"])
                for command in commands:
                    ipc(backend_ipc, 0, command)
                return bool(commands)

            def apply_output_preferences(outputs):
                """Extend mode: every screen, including ones plugged in later, takes its KDE (or
                known panel) rotation and scale. Only differences are sent, so polling never loops."""
                changed = False
                for name, preference in output_preferences(outputs).items():
                    current = next((o for o in outputs if o.get("name") == name and o.get("active")), None)
                    if current is None:
                        continue
                    for key in ("transform", "scale"):
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
                    description = describe_displays(outputs, preferred_primary)
                except ValueError as error:
                    # Unplugging every display must not kill applications. Keep
                    # the last valid layout until usable outputs return.
                    message = str(error)
                    if state.get("display_error") != message:
                        state["display_error"] = message
                        record()
                    return
                if description != display_description:
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
                                    if startup_chime(child_environment):
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

            def lock():
                nonlocal locker, next_lock_retry
                if locker and locker.poll() is None and state.get("lock_ready"):
                    return True
                # A failed/abandoned lock is not an authentication result. Keep
                # logout guarded while reacquiring the compositor's lock, even
                # when an attempt times out before its ready notification.
                state.update(locked=True, lock_ready=False)
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
                idle = subprocess.Popen(idle_command(lock_command),
                                        env=child_environment, stdin=subprocess.DEVNULL,
                                        stdout=idle_log, stderr=subprocess.STDOUT,
                                        start_new_session=True)
            deadline = time.monotonic() + 15
            next_display_poll = 0
            while not stopping and backend.poll() is None and desktop.poll() is None:
                inspect_displays = options.experimental_extended and time.monotonic() >= next_display_poll
                if inspect_displays:
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
                            lock()
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
                                        preferred_primary = requested
                                        update_displays()
                                        ok = state.get("primary_output") == requested and not state.get("display_error")
                                        if ok:
                                            primary_chosen_here = True  # KDE's main display no longer applies.
                                            state["requested_primary_output"] = requested
                                            record()
                                        else:
                                            preferred_primary = previous_preference
                                            update_displays()
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
    parser.add_argument("--control", type=Path, help="Send lock/status/logout or experimental primary choice to an owned session")
    parser.add_argument("command", nargs="?", choices=("lock", "status", "logout", "primary"))
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
    options = parser.parse_args()
    if options.primary_output and not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", options.primary_output):
        parser.error("Invalid output connector name")
    if options.output_scale is not None and not 0.5 <= options.output_scale <= 4:
        parser.error("Output scale must be between 0.5 and 4")
    if options.experimental_extended and options.single_output:
        parser.error("--experimental-extended cannot be combined with --single-output")
    if options.control:
        if not options.command:
            parser.error("--control requires lock, status, logout or primary")
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
