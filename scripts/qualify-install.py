#!/usr/bin/env python3
"""Exercise a staged installation with disposable data; do not install to /usr."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("prefix", type=Path, help="Staged prefix containing bin/platinum-reality")
parser.add_argument("--evidence", type=Path, required=True)
options = parser.parse_args()
prefix = options.prefix.resolve()
launcher = prefix / "bin/platinum-reality"
evidence = options.evidence.resolve()
evidence.mkdir(parents=True, exist_ok=False)
library = prefix / "lib/platinum-reality"
expected = [launcher, *(library / n for n in (
    "frankenstein", "frankenstein-finder", "frankenstein-editor", "frankenstein-drop-receiver", "frankenstein-puzzle",
    "plugins/styles/libfrankenstein-style.so", "session_support.py", "system_controls.py", "audio_controls.py", "platinum-wallet-service")),
    prefix / "share/applications/platinum-reality.desktop",
    prefix / "share/icons/hicolor/scalable/apps/platinum-reality.svg"]
# The whole session, as login screens start it.
session = [prefix / "bin/platinum-reality-session", library / "standalone-session.py",
           prefix / "share/wayland-sessions/platinum-reality.desktop"]
for path in expected + session:
    if not path.is_file():
        raise SystemExit("Missing installed file: " + str(path))
record = {"installed_sha256": {
    str(p.relative_to(prefix)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted(prefix.rglob("*")) if p.is_file()
}, "checks": {}}
subprocess.run(["desktop-file-validate", str(expected[-2])], check=True)
record["checks"]["desktop_entry"] = True
subprocess.run([str(launcher), "--help"], check=True, stdout=subprocess.DEVNULL)
record["checks"]["help"] = True
entry = session[2].read_text()
assert "\nExec=platinum-reality-session\n" in "\n" + entry and "Type=Application" in entry, entry
subprocess.run([str(session[0]), "--help"], check=True, stdout=subprocess.DEVNULL)
record["checks"]["login_screen_session"] = True
x11_enabled = (library / "xwayland-satellite").is_file()
if x11_enabled:
    provenance = json.loads((library / "xwayland-satellite-provenance.json").read_text())
    assert hashlib.sha256((library / "xwayland-satellite").read_bytes()).hexdigest() == provenance["patched_binary_sha256"]
    assert (prefix / "share/licenses/xwayland-satellite/LICENSE").is_file()
    source = prefix / "share/doc/platinum-reality/xwayland-satellite"
    assert hashlib.sha256((source / "source.tar.gz").read_bytes()).hexdigest() == provenance["sha256"]
    assert hashlib.sha256((source / "Cargo.lock").read_bytes()).hexdigest() == provenance["cargo_lock_sha256"]
    assert hashlib.sha256((source / "xwayland-satellite-qt-shell-v1.patch").read_bytes()).hexdigest() == provenance["compatibility_patch_sha256"]
    record["checks"]["x11_binary_license_and_corresponding_source"] = True
    # The private Xwayland launcher and its uninstrumented preload library.
    xwayland = library / "xwayland"
    assert os.access(xwayland / "Xwayland", os.X_OK)
    shim = xwayland / "libplatinum-xwayland-implicit-sync.so"
    assert shim.is_file()
    dependencies = subprocess.run(["ldd", str(shim)], check=True, capture_output=True, text=True).stdout
    assert "libasan" not in dependencies and "libubsan" not in dependencies, dependencies
    exported = subprocess.run(["nm", "-D", "--defined-only", str(shim)], check=True, capture_output=True, text=True).stdout
    assert [line.split()[-1] for line in exported.splitlines()] == ["drmSyncobjEventfd"], exported
    record["checks"]["x11_xwayland_implicit_sync_launcher"] = True
with tempfile.TemporaryDirectory(prefix="platinum-install-check-") as temporary:
    fixture = Path(temporary)
    environment = os.environ.copy()
    # A missing parent display must fail before creating a profile or workspace.
    absent = environment.copy()
    absent["XDG_DATA_HOME"] = str(fixture / "data")
    absent.pop("WAYLAND_DISPLAY", None)
    absent.pop("DISPLAY", None)
    result = subprocess.run([str(launcher)], env=absent, capture_output=True)
    assert result.returncode != 0 and not (fixture / "data").exists()
    record["checks"]["missing_display_fails_without_profile"] = True
    for mode in ("windowed", "fullscreen"):
        image = evidence / (mode + ".png")
        state = evidence / (mode + ".json")
        args = [str(launcher), "--workspace", str(fixture / "Workspace with spaces"),
                "--profile", str(fixture / "Profile with spaces"), "--no-finder",
                "--exit-after", "3000", "--screenshot", str(image), "--state-file", str(state)]
        if mode == "windowed":
            args.append("--windowed")
        with (evidence / (mode + ".txt")).open("wb") as output:
            child = subprocess.Popen(args, env=environment, stdout=output, stderr=subprocess.STDOUT)
            try:
                limit = time.monotonic() + 10
                while not image.exists() and child.poll() is None and time.monotonic() < limit:
                    time.sleep(0.05)
                assert image.exists(), "Installed desktop did not produce its live capture"
                assert (fixture / "Profile with spaces").stat().st_mode & 0o777 == 0o700
                duplicate = subprocess.run(args, env=environment, capture_output=True, timeout=5)
                assert duplicate.returncode != 0 and b"already has an active session" in duplicate.stderr
                assert child.wait(timeout=10) == 0
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
        assert state.is_file() and state.stat().st_size > 0
        if x11_enabled:
            observation = json.loads(state.read_text())["x11"]
            assert observation["state"] == "ready" and observation["display"].startswith(":")
            record["checks"][mode + "_private_x11_ready"] = True
        record["checks"][mode + "_native_launch_and_cleanup"] = True
        record["checks"][mode + "_duplicate_profile_rejected"] = True
        record["checks"][mode + "_new_profile_owner_only"] = True
# The installed Sound backend against a disposable PipeWire server with virtual devices only
# (tests/owned_audio_server.py): no hardware modules, WirePlumber, host bus or host audio socket.
audio_fixture = Path(__file__).resolve().parent.parent / "tests/owned_audio_server.py"
record["fixture_sha256"] = {"tests/owned_audio_server.py": hashlib.sha256(audio_fixture.read_bytes()).hexdigest()}
with tempfile.TemporaryDirectory(prefix="platinum-sound-check-") as temporary:
    home = Path(temporary)
    server = subprocess.Popen(["/usr/bin/python3", "-I", str(audio_fixture), "--owner-pid", str(os.getpid()),
                               "--evidence", str(evidence / "audio")], stdout=subprocess.PIPE, text=True)
    try:
        ready = server.stdout.readline()
        assert ready.startswith("audio-ready "), ready
        audio_environment = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "HOME": str(home),
                             "XDG_RUNTIME_DIR": str(home), "XDG_CONFIG_HOME": str(home / "config"),
                             "DBUS_SESSION_BUS_ADDRESS": "unix:path=" + str(home / "no-session-bus"),
                             "DBUS_SYSTEM_BUS_ADDRESS": "unix:path=" + str(home / "no-system-bus"),
                             "PULSE_SERVER": json.loads(ready[len("audio-ready "):])["server"]}

        def sound(*arguments):
            reply = subprocess.run(["/usr/bin/python3", "-I", str(library / "audio_controls.py"), *arguments],
                                   env=audio_environment, capture_output=True, text=True, timeout=30)
            (evidence / "audio" / ("sound-" + "-".join(arguments) + ".json")).write_text(reply.stdout)
            assert reply.returncode == 0, reply.stdout
            return json.loads(reply.stdout)["state"]

        state = sound("status")
        assert [o["name"] for o in state["outputs"]] == ["fixture_headphones", "fixture_speakers"], state
        # Monitors of outputs are not offered as microphones.
        assert [i["name"] for i in state["inputs"]] == ["fixture_microphone"], state
        assert state["defaultOutput"] == "fixture_speakers" and state["defaultInput"] == "fixture_microphone"
        record["checks"]["installed_sound_lists_virtual_devices"] = True
        state = sound("output", "fixture_headphones")
        assert state["defaultOutput"] == "fixture_headphones", state
        state = sound("volume", "output", "fixture_headphones", "37")
        assert next(o for o in state["outputs"] if o["name"] == "fixture_headphones")["volume"] == 37, state
        state = sound("mute", "input", "fixture_microphone", "1")
        assert next(i for i in state["inputs"] if i["name"] == "fixture_microphone")["muted"] is True, state
        record["checks"]["installed_sound_switches_output_sets_volume_and_mutes"] = True
    finally:
        server.terminate()
        server.wait(timeout=10)
    cleanup = json.loads((evidence / "audio/audio-fixture-cleanup.json").read_text())
    assert cleanup["owned_server_reaped"] and not cleanup["hardware_loaded"]
    record["checks"]["owned_audio_server_reaped_without_hardware"] = True
(evidence / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
print("Staged installation checks passed. Evidence: " + str(evidence))
