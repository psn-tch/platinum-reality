#!/usr/bin/env python3
"""Qualify wallet routing with fake services and a disposable HOME on an owned headless display.

Never invokes the real wallet or opens credentials. Run through with-headless-display.py.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import sys
import tempfile
import time

from session_support import WALLET_SERVICES, cleanup_runtime, write_bus_config


def wait_for(predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("Wallet fixture did not become ready before timeout")


def absent(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    # A daemon's orphaned supervisor can briefly await its host subreaper.
    try:
        return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0] == "Z"
    except FileNotFoundError:
        return True


def stop(child):
    if child is None:
        return
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=8)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)


def state(path):
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def call(address, destination, method, *arguments, check=True):
    return subprocess.run(["gdbus", "call", "--address", address, "--dest", destination,
                           "--object-path", "/org/platinum_reality/WalletFixture" if
                           method.startswith("org.platinum_reality.") else "/org/freedesktop/DBus",
                           "--method", method, *arguments], check=check, capture_output=True,
                          text=True, timeout=10)


def run(options):
    prefix = options.prefix.resolve()
    library = prefix / "lib/platinum-reality"
    evidence = options.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    project = Path(__file__).resolve().parents[1]
    fake = project / "tests/fake_wallet.py"
    record = {"time_utc": datetime.now(timezone.utc).isoformat(), "checks": {}, "real_wallet_opened": False,
              "hardware_qualified": False, "payload_sha256": {
                  str(p.relative_to(prefix)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(library.rglob("*")) if p.is_file()},
              "source_sha256": {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (Path(__file__).resolve(), fake,
                                          project / "scripts/session_support.py")}}
    for scenario in ("normal-bus-exit", "abrupt-bus-exit"):
        runtime = Path(tempfile.mkdtemp(prefix="frankenstein-runtime."))
        bus = desktop = None
        services = []
        with tempfile.TemporaryDirectory(prefix="platinum-wallet-fixture-") as temporary:
            fixture = Path(temporary)
            home = fixture / "home"
            for suffix in ("config", "data", "cache", "state"):
                (home / suffix).mkdir(parents=True)
            environment = os.environ.copy()
            for key in list(environment):
                if key.startswith("PLATINUM_WALLET_") or key.startswith("QT_"):
                    environment.pop(key)
            environment.pop("FRANKENSTEIN_NATIVE_TEST", None)
            environment.update(HOME=str(home), XDG_CONFIG_HOME=str(home / "config"),
                               XDG_DATA_HOME=str(home / "data"), XDG_CACHE_HOME=str(home / "cache"),
                               XDG_STATE_HOME=str(home / "state"), XDG_RUNTIME_DIR=str(runtime),
                               DBUS_SYSTEM_BUS_ADDRESS="unix:path=" + str(fixture / "no-host-system-bus"),
                               PLATINUM_LOGIN_SESSION="1", PLATINUM_WALLET_REQUESTED="1",
                               PLATINUM_WALLET_SERVICES="1", QT_QPA_PLATFORM="wayland",
                               FRANKENSTEIN_ISOLATED="1")
            parent = os.environ["WAYLAND_DISPLAY"]
            if not Path(parent).is_absolute():
                parent = str(Path(os.environ["XDG_RUNTIME_DIR"]) / parent)
            environment["WAYLAND_DISPLAY"] = parent
            source = fixture / "services"
            source.mkdir()
            events = evidence / (scenario + "-services.jsonl")
            for name in (*WALLET_SERVICES, "org.example.Unrelated"):
                command = [sys.executable, "-I", str(fake), "--name", name, "--log", str(events)]
                if name == "org.kde.secretprompter":
                    command.append("--window")
                (source / (name + ".service")).write_text(
                    "[D-BUS Service]\nName=" + name + "\nExec=" + shlex.join(command) + "\n")
            config = write_bus_config(runtime, wallet_supervisor=library / "platinum-wallet-service",
                                      service_directory=source)
            try:
                with (evidence / (scenario + "-bus.txt")).open("wb") as bus_log, \
                        (evidence / (scenario + "-desktop.txt")).open("wb") as desktop_log:
                    bus = subprocess.Popen(["dbus-daemon", "--nofork", "--print-address",
                                            "--config-file=" + str(config)], env=environment,
                                           stdout=subprocess.PIPE, stderr=bus_log, text=True)
                    address = bus.stdout.readline().strip()
                    assert address.startswith("unix:"), "The private wallet bus did not start"
                    environment["DBUS_SESSION_BUS_ADDRESS"] = address
                    observation = evidence / (scenario + "-state.json")
                    desktop = subprocess.Popen([str(library / "frankenstein"), "--workspace", str(fixture / "Workspace"),
                                                "--profile", str(fixture / "Profile"), "--application-settings", "normal",
                                                "--no-finder", "--state-file", str(observation), "--fullscreen"],
                                               env=environment, stdout=desktop_log, stderr=subprocess.STDOUT)
                    ready = wait_for(lambda: s if (s := state(observation)).get("wallet", {}).get("ready") and
                                     s.get("x11", {}).get("state") == "ready" else None)
                    assert not events.exists(), "Configuring the environment must not open a wallet"
                    record["checks"][scenario + "_ready_without_activating_wallet"] = True
                    for name in WALLET_SERVICES:
                        result = call(address, name, "org.platinum_reality.WalletFixture.Ready")
                        assert name in result.stdout
                    wait_for(lambda: any(w.get("title") == "Wallet unlock fixture"
                                         for w in state(observation).get("windows", [])))
                    record["checks"][scenario + "_prompt_is_a_native_desktop_window"] = True
                    entries = [json.loads(line) for line in events.read_text().splitlines()]
                    services = [entry for entry in entries if entry["event"] == "started"]
                    assert {entry["name"] for entry in services} == set(WALLET_SERVICES)
                    for entry in services:
                        child_environment = entry["environment"]
                        assert child_environment["HOME"] == str(home)
                        assert child_environment["XDG_CONFIG_HOME"] == str(home / "config")
                        assert child_environment["XDG_DATA_HOME"] == str(home / "data")
                        assert child_environment["XDG_RUNTIME_DIR"] == str(runtime)
                        assert child_environment["DBUS_SESSION_BUS_ADDRESS"] == address
                        assert child_environment["WAYLAND_DISPLAY"] != parent
                        assert (runtime / child_environment["WAYLAND_DISPLAY"]).is_socket()
                        assert child_environment["QT_QPA_PLATFORM"] == "wayland"
                        assert child_environment["DISPLAY"] == ready["x11"]["display"]
                        assert Path(child_environment["XAUTHORITY"]).is_file()
                        assert entry["wayland_socket_present"] is False
                        assert Path(f"/proc/{entry['supervisor_pid']}/exe").resolve() == (library / "platinum-wallet-service").resolve()
                    record["checks"][scenario + "_private_display_bus_settings_and_supervisor"] = True
                    refused = call(address, "org.example.Unrelated", "org.platinum_reality.WalletFixture.Ready", check=False)
                    assert refused.returncode != 0 and "ServiceUnknown" in refused.stderr
                    record["checks"][scenario + "_unrelated_service_cannot_activate"] = True
                    if scenario == "normal-bus-exit":
                        # Abruptly losing one supervisor must also release its foreground child.
                        victim = next(entry for entry in services if entry["name"] == WALLET_SERVICES[0])
                        os.kill(victim["supervisor_pid"], signal.SIGKILL)
                        wait_for(lambda: absent(victim["pid"]))
                        record["checks"]["supervisor_loss_releases_its_daemon"] = True
                        bus.terminate()
                    else:
                        bus.kill()
                    bus.wait(timeout=5)
                    for entry in services:
                        wait_for(lambda entry=entry: absent(entry["pid"]) and absent(entry["supervisor_pid"]))
                    record["checks"][scenario + "_wallet_processes_exit_with_private_bus"] = True
                    wait_for(lambda: not state(observation).get("wallet", {}).get("ready", True))
                    record["checks"][scenario + "_desktop_withdraws_wallet_readiness"] = True
            finally:
                stop(desktop)
                stop(bus)
                # A service can fail before Ready returns. Still account for
                # every fixture that started, rather than skipping their cleanup.
                if events.exists():
                    services = [entry for line in events.read_text().splitlines()
                                if (entry := json.loads(line))["event"] == "started"]
                for entry in services:
                    # Cleanup only exact fixture-owned PIDs whose executable still matches.
                    for pid, executable in ((entry["pid"], Path(sys.executable).resolve()),
                                            (entry["supervisor_pid"], (library / "platinum-wallet-service").resolve())):
                        if not absent(pid) and Path(f"/proc/{pid}/exe").resolve() == executable:
                            os.kill(pid, signal.SIGTERM)
                            wait_for(lambda pid=pid: absent(pid))
                # Qt's killed compositor may leave a Wayland lock, retained as evidence.
                if not cleanup_runtime(runtime):
                    unexpected = sorted(p.name for p in runtime.iterdir())
                    record.setdefault("retained_runtimes", []).append({"runtime": str(runtime), "entries": unexpected})
                (evidence / "partial-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    assert all(record["checks"].values())
    (evidence / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print("Disposable wallet qualification passed:", len(record["checks"]), "checks. Evidence:", evidence)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    options = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="platinum-wallet-preflight-") as temporary:
            with socket.socket(socket.AF_UNIX) as probe:
                probe.bind(str(Path(temporary) / "probe"))
                probe.listen(1)
    except OSError as error:
        print("Wallet qualification venue unavailable (private Unix sockets): " + str(error), file=sys.stderr)
        raise SystemExit(77)
    run(options)
