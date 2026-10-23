#!/usr/bin/env python3
"""Disposable PipeWire/Pulse fixture: virtual nodes only, no hardware or host bus.

A minimal fixture policy copies configured defaults to applied defaults. This is
not WirePlumber qualification. The owning native test supplies its exact PID.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

stopping = False


def request_stop(_signal, _frame):
    global stopping
    stopping = True


def attach_to_parent(parent):
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGTERM, 0, 0, 0) != 0 or os.getppid() != parent:
        os._exit(2)


def run(owner, evidence):
    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    attach_to_parent(owner)
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="platinum-audio-fixture-") as temporary:
        home = Path(temporary)
        runtime = home / "runtime"
        runtime.mkdir(mode=0o700)
        pulse = runtime / "pulse-native"
        config = home / "fixture.conf"
        config.write_text('''context.properties = { core.daemon = true core.name = pipewire-0 support.dbus = false }
context.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }
context.modules = [
 { name = libpipewire-module-protocol-native }
 { name = libpipewire-module-access args = { access.socket = { pipewire-0 = unrestricted pipewire-0-manager = unrestricted } } }
 { name = libpipewire-module-metadata }
 { name = libpipewire-module-spa-node-factory }
 { name = libpipewire-module-client-node }
 { name = libpipewire-module-link-factory }
 { name = libpipewire-module-adapter }
 { name = libpipewire-module-protocol-pulse args = { server.address = [ "unix:''' + str(pulse) + '''" ] } }
]
context.exec = []
pulse.cmd = []
context.objects = [
 { factory = metadata args = { metadata.name = default } }
 { factory = spa-node-factory args = { factory.name = support.node.driver node.name = Fixture-Driver priority.driver = 20000 } }
 { factory = adapter args = { factory.name = support.null-audio-sink node.name = fixture_speakers node.description = "Fixture Speakers" media.class = Audio/Sink audio.position = [ FL FR ] } }
 { factory = adapter args = { factory.name = support.null-audio-sink node.name = fixture_headphones node.description = "Fixture Headphones" media.class = Audio/Sink audio.position = [ FL FR ] } }
 { factory = adapter args = { factory.name = support.null-audio-sink node.name = fixture_microphone node.description = "Fixture Microphone" media.class = Audio/Source/Virtual audio.position = [ FL FR ] } }
]
''')
        (evidence / "audio-fixture-config.txt").write_text(config.read_text())
        environment = {
            "HOME": str(home), "PATH": "/usr/bin:/bin", "LANG": "C.UTF-8",
            "XDG_RUNTIME_DIR": str(runtime), "PIPEWIRE_RUNTIME_DIR": str(runtime),
            "PIPEWIRE_REMOTE": "pipewire-0", "PIPEWIRE_CONFIG_DIR": str(home),
            "XDG_CONFIG_HOME": str(home / "config"), "XDG_DATA_HOME": str(home / "data"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "DBUS_SYSTEM_BUS_ADDRESS": "unix:path=" + str(home / "no-system-bus"),
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=" + str(home / "no-session-bus"),
            "PULSE_SERVER": "unix:" + str(pulse),
        }
        client_environment = {k: v for k, v in environment.items() if k != "PIPEWIRE_CONFIG_DIR"}
        parent = os.getpid()
        with (evidence / "audio-fixture-server.txt").open("wb") as log:
            server = subprocess.Popen(["/usr/bin/pipewire", "-c", str(config)], env=environment,
                                      stdout=log, stderr=subprocess.STDOUT,
                                      preexec_fn=lambda: attach_to_parent(parent))
            def metadata(*arguments):
                return subprocess.run(["/usr/bin/pw-metadata", "-n", "default", *arguments],
                                      env=client_environment, check=True, capture_output=True,
                                      text=True, timeout=2).stdout
            def apply_policy():
                values = {}
                for line in metadata().splitlines():
                    match = re.search(r"key:'([^']+)' value:'([^']*)'", line)
                    if match:
                        values[match[1]] = match[2]
                assert all("default.configured.audio." + kind in values for kind in ("sink", "source")), values
                for kind, allowed in (("sink", {"fixture_speakers", "fixture_headphones"}),
                                      ("source", {"fixture_microphone"})):
                    configured = values.get("default.configured.audio." + kind)
                    if not configured:
                        continue
                    value = json.loads(configured)
                    assert value.get("name") in allowed, "Non-fixture default requested"
                    if values.get("default.audio." + kind) != configured:
                        metadata("0", "default.audio." + kind, configured, "Spa:String:JSON")
            try:
                deadline = time.monotonic() + 5
                while not pulse.is_socket():
                    assert server.poll() is None and time.monotonic() < deadline
                    time.sleep(0.025)
                for kind, name in (("sink", "fixture_speakers"), ("source", "fixture_microphone")):
                    metadata("0", "default.configured.audio." + kind, json.dumps({"name": name}), "Spa:String:JSON")
                apply_policy()
                print("audio-ready " + json.dumps({"server": environment["PULSE_SERVER"],
                                                    "pid": server.pid}), flush=True)
                while not stopping:
                    assert server.poll() is None, "Owned audio server exited"
                    apply_policy()
                    time.sleep(0.025)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=3)
                (evidence / "audio-fixture-cleanup.json").write_text(json.dumps({
                    "owned_server_reaped": True, "hardware_loaded": False,
                    "policy": "virtual defaults only; no WirePlumber"}) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--owner-pid", type=int, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    options = parser.parse_args()
    try:
        run(options.owner_pid, options.evidence)
    except Exception as error:
        print("audio-error " + str(error), flush=True)
        raise
