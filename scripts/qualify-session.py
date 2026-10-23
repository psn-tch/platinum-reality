#!/usr/bin/env python3
"""Live nested qualification of the proposed login backend. Never takes a VT.

Lock release uses SIGUSR1 solely on the test's own disposable swaylock process;
password authentication, suspend/resume and physical DRM/seat remain untested.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time

from session_support import write_bus_config


def wait_for(predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("Session condition did not become true before timeout")


def state(path):
    return json.loads(path.read_text()) if path.exists() else {}


def control(path, command):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(8)
        peer.connect(path)
        peer.sendall(command.encode() + b"\n")
        return json.loads(peer.recv(8192))


def stop(services):
    for service in reversed(services):
        if service.poll() is None:
            service.terminate()
            service.wait(timeout=15)


def absent(pid):
    try:
        os.kill(pid, 0)
        return False
    except ProcessLookupError:
        return True


def run(options):
    runner = Path(__file__).resolve().with_name("standalone-session.py")
    fake_bluez = Path(__file__).resolve().parent.parent / "tests/fake_bluez.py"
    launcher = options.launcher.resolve()
    evidence = options.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    record = {"checks": {}, "hardware_qualified": False, "password_authentication_tested": False,
              "sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (runner, Path(__file__).resolve(), launcher, fake_bluez)}}
    library = launcher.parent.parent / "lib/platinum-reality"
    record["payload_sha256"] = {
        str(p.relative_to(launcher.parent.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(library.rglob("*")) if p.is_file()}
    record["retained_inner_runtimes"] = []
    record["versions"] = {
        command: subprocess.run([command, flag], capture_output=True, text=True, check=True).stdout.strip()
        for command, flag in (("sway", "--version"), ("swaylock", "--version"))}
    # Refuse to take the real seat when invoked from an existing desktop.
    refused = subprocess.run(["python3", str(runner), "--launcher", str(launcher),
                              "--evidence", str(evidence / "must-not-exist")], capture_output=True)
    assert refused.returncode != 0 and b"graphical session is already active" in refused.stderr
    assert not (evidence / "must-not-exist").exists()
    record["checks"]["active_desktop_guard"] = True
    with tempfile.TemporaryDirectory(prefix="platinum-login-check-") as temporary:
        fixture = Path(temporary)
        # Applications use normal settings folders in the installed product: keep them disposable.
        home = fixture / "home"
        # A nested lock must never claim the user's real fingerprint reader.
        # HOME/profile isolation does not isolate QDBusConnection::systemBus().
        # No authentication is attempted here; fake PAM/fprintd coverage lives
        # in the dedicated lock tests. Keep all supervisor descendants off the
        # host system bus, including the default Platinum locker.
        private_system_bus = "unix:path=" + str(fixture / "no-host-system-bus")
        for name in ("config", "data", "cache", "state"):
            (home / name).mkdir(parents=True)
        os.environ.update(HOME=str(home), XDG_CONFIG_HOME=str(home / "config"), XDG_DATA_HOME=str(home / "data"),
                          XDG_CACHE_HOME=str(home / "cache"), XDG_STATE_HOME=str(home / "state"),
                          DBUS_SYSTEM_BUS_ADDRESS=private_system_bus)
        kde_outputs = home / "config" / "kwinoutputconfig.json"
        for scenario in ("lock-and-logout", "locker-startup-recovery", "platinum-lock", "all-displays",
                         "bluetooth", "backend-crash", "desktop-crash", "startup-failure"):
            run_dir = evidence / scenario
            if scenario == "all-displays":
                # The Monitors panel's "Use all displays", saved in the desktop profile, must turn
                # the login's one-screen request into the extended session.
                profile = fixture / scenario / "Profile"
                profile.mkdir(parents=True)
                (profile / "displays.json").write_text(json.dumps({"version": 1, "mode": "extend"}))
            if scenario == "lock-and-logout":
                # A saved keep-awake preference must become a host inhibitor
                # after startup, without preventing an explicit manual lock.
                profile = fixture / scenario / "Profile"
                profile.mkdir(parents=True)
                (profile / "appearance.json").write_text(json.dumps({
                    "version": 1, "shadeDuration": 240, "stripVisible": True,
                    "stripCollapsed": False, "stripX": 8, "stripY": -1,
                    "keepScreenAwake": True}))
            # One scenario carries the user's KDE screen settings: a portrait panel KDE shows
            # rotated (Rotated90, which is Sway's 270) at 2x, instead of --output-scale.
            kde_settings = scenario == "desktop-crash"
            if kde_settings:
                kde_outputs.write_text(json.dumps([{"name": "outputs", "data": [
                    {"connectorName": "WL-1", "transform": "Rotated90", "scale": 2.0}]}]))
            elif kde_outputs.exists():
                kde_outputs.unlink()
            expected_scale, expected_transform = (2.0, "270") if kde_settings else (1.5, "normal")
            args = ["python3", str(runner), "--nested-check", "--launcher", str(launcher),
                    "--evidence", str(run_dir), "--workspace", str(fixture / scenario / "Workspace"),
                    "--profile", str(fixture / scenario / "Profile"),
                    "--primary-output", "DP-2" if scenario == "backend-crash" else "WL-1",
                    "--output-scale", "1.5", "--single-output"]
            # These scenarios release the lock with swaylock's own SIGUSR1; the Platinum lock has
            # no such signal, so its scenario uses the production default and crashes it instead.
            if scenario != "platinum-lock":
                args.extend(["--locker", "swaylock"])
            if scenario == "startup-failure":
                args.append("--shell-argument=--invalid-option-for-qualification")
            else:
                args.extend(["--shell-argument=--screenshot",
                             "--shell-argument=" + str(run_dir / "desktop.png")])
            environment = os.environ.copy()
            failed_start = fixture / "fail-locker-start"
            if scenario == "locker-startup-recovery":
                # Simulate a launcher exiting successfully WITHOUT securing the
                # display. This is not authentication, even though exit == 0.
                # Only this disposable supervisor sees the controlled wrapper.
                real_locker = shutil.which("swaylock")
                assert real_locker
                failed_start.touch()
                wrapper = fixture / "bin" / "swaylock"
                wrapper.parent.mkdir()
                wrapper.write_text("#!/usr/bin/python3\nimport os,sys\nfrom pathlib import Path\n"
                                   + f"if Path({str(failed_start)!r}).exists(): sys.exit(0)\n"
                                   + f"os.execv({real_locker!r}, [{real_locker!r}] + sys.argv[1:])\n")
                wrapper.chmod(0o700)
                environment["PATH"] = str(wrapper.parent) + os.pathsep + environment["PATH"]
            bluetooth_services = []
            if scenario == "bluetooth":
                # A fake BlueZ on its own private bus stands in for the system bus: the installed
                # desktop must become the default pairing agent there (docs/bluetooth.md).
                # The nested sessions' private bus configuration, in the fixture: the session
                # configuration would add service directories under the headless runtime.
                bus_directory = fixture / scenario / "bluetooth-bus"
                bus_directory.mkdir(parents=True)
                try:
                    bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(write_bus_config(bus_directory)),
                                            "--nofork", "--print-address"], stdout=subprocess.PIPE, text=True)
                    bluetooth_services.append(bus)
                    bluetooth_bus = bus.stdout.readline().strip()
                    assert bluetooth_bus.startswith("unix:")
                    bluetooth_log = evidence / "bluetooth-bluez.jsonl"
                    bluez = subprocess.Popen(["python3", "-I", str(fake_bluez), "--address", bluetooth_bus,
                                              "--log", str(bluetooth_log)], stdout=subprocess.PIPE, text=True)
                    bluetooth_services.append(bluez)
                    assert bluez.stdout.readline().strip() == "ready"
                except BaseException:
                    # A failed start must not leave the private bus or fake BlueZ running.
                    stop(bluetooth_services)
                    raise
                environment["DBUS_SYSTEM_BUS_ADDRESS"] = bluetooth_bus
            with (evidence / (scenario + "-supervisor.txt")).open("wb") as log:
                child = subprocess.Popen(args, env=environment, stdout=log, stderr=subprocess.STDOUT)
                try:
                    state_file = run_dir / "session.json"
                    if scenario == "startup-failure":
                        assert child.wait(timeout=20) != 0
                    else:
                        ready = wait_for(lambda: s if (s := state(state_file)).get("ready") else None)
                        backend_pid = ready["backend_pid"]
                        if scenario == "all-displays":
                            assert ready["display_mode"] == "extend" and ready["primary_output"] == "WL-1"
                            assert Path(ready["display_topology"]).is_file()
                            assert ready["display_applied_generation"] == ready["display_generation"]
                            record["checks"]["saved_choice_uses_all_displays"] = True
                        else:
                            assert ready["primary_output"] == "WL-1" and ready["output_scale"] == expected_scale
                            assert ready["output_transform"] == expected_transform
                        if scenario == "backend-crash":
                            assert ready["preferred_output_missing"] == "DP-2"
                            assert ready["output_selection_reason"] == "fallback_external"
                            record["checks"]["renamed_output_gets_requested_scale"] = True
                        output_reply = subprocess.run(["swaymsg", "-s", ready["backend_ipc"],
                            "-r", "-t", "get_outputs"], capture_output=True, text=True, check=True)
                        outputs = json.loads(output_reply.stdout)
                        assert any(o["name"] == "WL-1" and o["active"] and o["scale"] == expected_scale
                                   and o["transform"] == expected_transform for o in outputs) \
                            or scenario == "all-displays" and any(o["name"] == "WL-1" and o["active"] for o in outputs)
                        if kde_settings:
                            assert ready["output_settings_source"] == "kde"
                            record["checks"]["screen_keeps_kde_rotation_and_scale"] = True
                        desktop_pids = ready["outer_window_pids"]
                        assert len(desktop_pids) == 1
                        assert control(ready["control"], "status")["ok"]
                        wait_for(lambda: (run_dir / "desktop.png").exists())
                        if scenario == "lock-and-logout":
                            def host_inhibited():
                                tree = subprocess.run(["swaymsg", "-s", ready["backend_ipc"],
                                                       "-r", "-t", "get_tree"],
                                                      capture_output=True, text=True, check=True)
                                def find(node):
                                    if node.get("pid") in desktop_pids and node.get("inhibit_idle"):
                                        return True
                                    return any(find(child) for key in ("nodes", "floating_nodes")
                                               for child in node.get(key, []))
                                return find(json.loads(tree.stdout))
                            wait_for(host_inhibited)
                            record["checks"]["saved_keep_awake_reaches_host"] = True
                            # A disconnected or malformed requester cannot end the desktop.
                            for payload in (b"", b"x" * 65, b"status"):
                                with socket.socket(socket.AF_UNIX) as peer:
                                    peer.connect(ready["control"])
                                    peer.sendall(payload)
                            assert control(ready["control"], "status")["ok"]
                            record["checks"]["malformed_control_does_not_end_session"] = True
                            locked = control(ready["control"], "lock")
                            assert locked["ok"] and locked["locked"]
                            assert not absent(locked["lock_pid"])
                            assert not control(ready["control"], "logout")["ok"]
                            record["checks"]["native_lock_readiness_and_logout_guard"] = True
                            # Lock attempts are timestamped in the session's own log, which a
                            # physical seat otherwise sends to tty1 (the 2026-10-21 unlocked sleep).
                            log = (run_dir / "supervisor.txt").read_text()
                            assert "lock: control request" in log and "lock: ready=True after" in log, log
                            record["checks"]["lock_attempts_logged"] = True
                            record["checks"]["manual_lock_works_while_keep_awake_enabled"] = True
                            # A dead locker must not be interpreted as authentication. The
                            # compositor keeps an abandoned ext-session-lock locked; the
                            # supervisor must retain that state and acquire a replacement.
                            old_lock = locked["lock_pid"]
                            os.kill(old_lock, signal.SIGKILL)
                            locked = wait_for(lambda: s if (s := control(ready["control"], "status")).get("locked")
                                              and s.get("lock_ready") and s.get("lock_pid") != old_lock else None)
                            assert not absent(locked["lock_pid"])
                            assert locked["lock_recoveries"] >= 1
                            assert not control(ready["control"], "logout")["ok"]
                            record["checks"]["locker_crash_recovers_without_unlocking"] = True
                            # Never signal a host lock; the PID comes from this disposable backend.
                            os.kill(locked["lock_pid"], signal.SIGUSR1)
                            wait_for(lambda: not control(ready["control"], "status")["locked"])
                            assert control(ready["control"], "logout")["ok"]
                            assert child.wait(timeout=15) == 0
                            record["checks"]["graceful_xdg_close_and_logout"] = True
                        elif scenario == "locker-startup-recovery":
                            pending = control(ready["control"], "lock")
                            assert not pending["ok"] and pending["locked"] and not pending["lock_ready"]
                            assert not control(ready["control"], "logout")["ok"]
                            # Observe an automatic failed retry: zero exit without
                            # ready must never be taken as a successful unlock.
                            time.sleep(2.2)
                            pending = control(ready["control"], "status")
                            assert pending["locked"] and not pending["lock_ready"] and pending.get("lock_error")
                            record["checks"]["unready_locker_cannot_unlock"] = True
                            failed_start.unlink()
                            locked = wait_for(lambda: s if (s := control(ready["control"], "status")).get("lock_ready") else None)
                            assert locked["locked"] and not absent(locked["lock_pid"])
                            assert not control(ready["control"], "logout")["ok"]
                            record["checks"]["locker_readiness_recovers_after_failed_start"] = True
                            os.kill(locked["lock_pid"], signal.SIGUSR1)
                            wait_for(lambda: not control(ready["control"], "status")["locked"])
                            assert control(ready["control"], "logout")["ok"]
                            assert child.wait(timeout=15) == 0
                        elif scenario == "platinum-lock":
                            locked = control(ready["control"], "lock")
                            assert locked["ok"] and locked["locked"] and locked["locker"] == "platinum"
                            # The release's own Platinum Log In lock holds the screen.
                            assert (Path(f"/proc/{locked['lock_pid']}/exe").resolve()
                                    == (launcher.parent.parent / "lib/platinum-reality/platinum-lock").resolve())
                            # Inspect only this test-owned locker. A readiness
                            # result alone does not prove hardware isolation.
                            locker_environment = Path(f"/proc/{locked['lock_pid']}/environ").read_bytes().split(b"\0")
                            assert ("DBUS_SYSTEM_BUS_ADDRESS=" + private_system_bus).encode() in locker_environment
                            record["checks"]["disposable_locker_cannot_reach_host_system_bus"] = True
                            assert not control(ready["control"], "logout")["ok"]
                            record["checks"]["platinum_lock_is_the_default_locker"] = True
                            # A crashed Platinum lock is never an unlock. It is replaced once; after a
                            # second failure in the same lock the supervisor falls back to swaylock.
                            for attempt, expected in ((1, "platinum"), (2, "swaylock")):
                                old_lock = locked["lock_pid"]
                                os.kill(old_lock, signal.SIGKILL)
                                locked = wait_for(lambda: s if (s := control(ready["control"], "status")).get("lock_ready")
                                                  and s.get("lock_pid") != old_lock else None)
                                assert locked["locked"] and not control(ready["control"], "logout")["ok"]
                                assert locked["platinum_lock_failures"] == attempt and locked["locker"] == expected
                            record["checks"]["platinum_lock_failures_fall_back_to_swaylock"] = True
                            os.kill(locked["lock_pid"], signal.SIGUSR1)
                            wait_for(lambda: not control(ready["control"], "status")["locked"])
                            assert "platinum_lock_failures" not in control(ready["control"], "status")
                            assert control(ready["control"], "logout")["ok"]
                            assert child.wait(timeout=15) == 0
                        elif scenario == "all-displays":
                            assert control(ready["control"], "logout")["ok"]
                            assert child.wait(timeout=15) == 0
                            # A normal logout after a session that was up: no one-display retry.
                            assert not (evidence / (scenario + "-one-display")).exists()
                        elif scenario == "bluetooth":
                            def bluez_log():
                                entries = ([json.loads(line) for line in bluetooth_log.read_text().splitlines()]
                                           if bluetooth_log.exists() else [])
                                # Complete once BlueZ has withdrawn its question.
                                return entries if any("cancel" in entry for entry in entries) else None
                            entries = wait_for(bluez_log, timeout=30)
                            calls = {x["call"]: x for x in entries if "call" in x}
                            assert calls["GetManagedObjects"]["pid"] in desktop_pids
                            assert calls["RegisterAgent"]["pid"] in desktop_pids
                            assert calls["RegisterAgent"]["arguments"][1] == "KeyboardDisplay"
                            assert calls["RequestDefaultAgent"]["pid"] in desktop_pids
                            record["checks"]["login_desktop_is_the_default_bluetooth_agent"] = True
                            outcomes = {key: x[key] for x in entries for key in ("impostor", "confirmation", "cancel")
                                        if key in x}
                            assert outcomes == {"impostor": "org.freedesktop.DBus.Error.AccessDenied",
                                                "confirmation": "timeout", "cancel": "ok"}, outcomes
                            record["checks"]["bluetooth_agent_waits_for_the_user_and_refuses_impostors"] = True
                            assert control(ready["control"], "status")["ok"]
                            assert control(ready["control"], "logout")["ok"]
                            assert child.wait(timeout=15) == 0
                        elif scenario == "backend-crash":
                            os.kill(backend_pid, signal.SIGKILL)
                            assert child.wait(timeout=15) != 0
                        else:
                            os.kill(desktop_pids[0], signal.SIGKILL)
                            assert child.wait(timeout=15) != 0
                        for pid in (backend_pid, *desktop_pids):
                            wait_for(lambda pid=pid: absent(pid))
                    finished = state(state_file)
                    assert finished["finished"] and finished["runtime_removed"]
                    assert not Path(finished["runtime"]).exists()
                    assert absent(finished["backend_pid"])
                    assert absent(finished["launcher_pid"])
                    record["checks"][scenario + "_cleanup"] = True
                    # A killed Qt compositor can leave its lock file. The qualified
                    # launcher deliberately retains unexpected files; do not hide that.
                    for line in (run_dir / "desktop.txt").read_text().splitlines():
                        if line.startswith("Runtime retained without recursive deletion: "):
                            record["retained_inner_runtimes"].append({"scenario": scenario,
                                "runtime": line.split(": ", 1)[1]})
                finally:
                    if child.poll() is None:
                        child.terminate()
                        child.wait(timeout=15)
                    stop(bluetooth_services)
    (evidence / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print("Session backend nested checks passed. Evidence: " + str(evidence))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launcher", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    run(parser.parse_args())
