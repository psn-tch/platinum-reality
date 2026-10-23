#!/usr/bin/env python3
"""Run GUI qualification on an owned headless Sway display, never the user's VT."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("session", Path(__file__).with_name("standalone-session.py"))
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--evidence", type=Path, required=True)
parser.add_argument("--timeout", type=int, default=180)
parser.add_argument("--scale", type=float, choices=(1.0, 1.5, 2.0), default=1.5,
                    help="Host display scale; keep a 1120x760 logical desktop")
parser.add_argument("--two-outputs", action="store_true",
                    help="Add a rotated, 2x secondary screen to the right (960x600 logical)")
parser.add_argument("command", nargs=argparse.REMAINDER)
options = parser.parse_args()
command = options.command[1:] if options.command[:1] == ["--"] else options.command
if not command:
    parser.error("A command is required after --")
options.evidence.mkdir(parents=True, exist_ok=False)
runtime = Path(tempfile.mkdtemp(prefix="platinum-headless."))
config = runtime / "sway.conf"
config.write_text(session.CONFIG)
environment = os.environ.copy()
for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "SWAYSOCK", "I3SOCK",
            "DBUS_SESSION_BUS_ADDRESS", "QT_QPA_PLATFORM", "QT_QUICK_BACKEND"):
    environment.pop(key, None)
# A terminal inside a desktop session (including Platinum Reality itself) inherits
# its client settings: installed plugin paths, shm-only buffers, the live profile's
# XDG roots and audio sockets. Tests must exercise the build, not the parent session.
inherited = sorted(key for key in environment if key.startswith(("QT_", "QSG_", "GDK_", "GTK_", "SDL_"))
                   or key in ("GSETTINGS_BACKEND", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME",
                              "XDG_STATE_HOME", "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP",
                              "PULSE_SERVER", "PIPEWIRE_RUNTIME_DIR"))
for key in inherited:
    environment.pop(key)
(options.evidence / "removed-environment.txt").write_text("".join(key + "\n" for key in inherited))
environment.update(XDG_RUNTIME_DIR=str(runtime), WLR_BACKENDS="headless",
                   WLR_LIBINPUT_NO_DEVICES="1", WLR_HEADLESS_OUTPUTS="2" if options.two_outputs else "1",
                   WLR_RENDERER="gles2")
backend = None
result = 1
with (options.evidence / "backend.txt").open("wb") as output:
    try:
        backend = subprocess.Popen(["sway", "--config", str(config)], env=environment,
            stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        ipc = runtime / f"sway-ipc.{os.getuid()}.{backend.pid}.sock"
        limit = time.monotonic() + 12
        while not ipc.exists():
            if backend.poll() is not None or time.monotonic() > limit:
                raise RuntimeError("Headless display did not start; see " + str(options.evidence))
            time.sleep(0.05)
        displays = [p for p in runtime.glob("wayland-*") if p.is_socket()]
        if len(displays) != 1:
            raise RuntimeError("Expected exactly one private headless display")
        # Match the existing native venue's 1120x760 logical desktop at 1.5x.
        settings = [f"output HEADLESS-1 mode --custom {int(1120*options.scale)}x{int(760*options.scale)}@60Hz",
                    f"output HEADLESS-1 scale {options.scale}", "output HEADLESS-1 position 0 0"]
        if options.two_outputs:
            settings.extend(["output HEADLESS-2 mode --custom 1200x1920@60Hz",
                             "output HEADLESS-2 scale 2", "output HEADLESS-2 transform 90",
                             "output HEADLESS-2 position 1120 0"])
        for setting in settings:
            reply = session.ipc(ipc, 0, setting)
            if not reply or not all(item.get("success") for item in reply):
                raise RuntimeError("Headless display rejected qualification geometry")
        (options.evidence / "display.json").write_text(json.dumps(session.ipc(ipc, 3), indent=2) + "\n")
        environment["WAYLAND_DISPLAY"] = str(displays[0])
        # An explicitly owned IPC endpoint for topology experiments. Never inherit
        # the real session's SWAYSOCK or expose it to qualification commands.
        environment["FRANKENSTEIN_HEADLESS_IPC"] = str(ipc)
        environment["QT_QPA_PLATFORM"] = "wayland"
        with (options.evidence / "command.txt").open("wb") as log:
            result = subprocess.run(command, env=environment, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=options.timeout).returncode
        print("Headless command exit:", result, "Evidence:", options.evidence)
    finally:
        if backend and backend.poll() is None:
            try:
                session.ipc(ipc, 0, "exit")
                backend.wait(timeout=3)
            except (OSError, ValueError, ConnectionError, subprocess.TimeoutExpired):
                session.stop_owned(backend)
        for entry in runtime.iterdir():
            mode = entry.lstat().st_mode
            if stat.S_ISSOCK(mode) or (stat.S_ISREG(mode) and
                    (entry.name == "sway.conf" or session.re.fullmatch(r"wayland-[0-9]+\.lock", entry.name))):
                entry.unlink()
        try:
            runtime.rmdir()
        except OSError:
            print("Headless runtime retained without recursive removal:", runtime, file=sys.stderr)
            result = 1
raise SystemExit(result)
