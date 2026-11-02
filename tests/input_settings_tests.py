"""The Keyboard and Mouse & Trackpad panels' input.json, as the session supervisor reads and
applies it. No Sway is started: the backend's IPC is replaced."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('standalone_session', Path(__file__).resolve().parents[1] / 'scripts/standalone-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)


class InputSettingsTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="platinum-input-test-")
        self.profile = Path(self.folder.name)

    def tearDown(self):
        self.folder.cleanup()

    def write(self, data):
        (self.profile / "input.json").write_text(json.dumps(data) if not isinstance(data, str) else data)

    def test_missing_or_damaged_files_change_nothing(self):
        self.assertEqual(session.saved_input_settings(self.profile), {})
        for damaged in ("{", "[]", json.dumps({"version": 2, "keyboard": {"repeatRate": 30}})):
            self.write(damaged)
            self.assertEqual(session.saved_input_settings(self.profile), {})
            self.assertEqual(session.input_commands(session.saved_input_settings(self.profile)), [])

    def test_a_symlink_or_oversized_file_is_ignored(self):
        target = self.profile / "elsewhere.json"
        target.write_text(json.dumps({"version": 1, "keyboard": {"repeatRate": 30}}))
        os.symlink(target, self.profile / "input.json")
        self.assertEqual(session.saved_input_settings(self.profile), {})
        (self.profile / "input.json").unlink()
        self.write(json.dumps({"version": 1, "keyboard": {"repeatRate": 30}, "pad": "x" * 5000}))
        self.assertEqual(session.saved_input_settings(self.profile), {})

    def test_only_values_in_range_become_sway_commands(self):
        self.write({"version": 1,
                    "keyboard": {"repeatDelay": 250, "repeatRate": 99},
                    "pointer": {"speed": 0.5, "natural": True, "leftHanded": "yes"},
                    "touchpad": {"speed": -2, "natural": False, "tap": True, "disableWhileTyping": False,
                                 "extra": "input * events disabled"}})
        settings = session.saved_input_settings(self.profile)
        self.assertEqual(settings, {"keyboard": {"repeatDelay": 250},
                                    "pointer": {"speed": 0.5, "natural": True},
                                    "touchpad": {"natural": False, "tap": True, "disableWhileTyping": False}})
        self.assertEqual(session.input_commands(settings), [
            "input type:keyboard repeat_delay 250",
            "input type:pointer pointer_accel 0.50",
            "input type:pointer natural_scroll enabled",
            "input type:touchpad natural_scroll disabled",
            "input type:touchpad tap enabled",
            "input type:touchpad dwt disabled",
        ])

    def test_booleans_are_not_numbers_and_left_handed_covers_both(self):
        self.write({"version": 1, "keyboard": {"repeatRate": True}, "pointer": {"leftHanded": True, "speed": False}})
        settings = session.saved_input_settings(self.profile)
        self.assertEqual(settings, {"pointer": {"leftHanded": True}})
        self.assertEqual(session.input_commands(settings),
                         ["input type:pointer left_handed enabled", "input type:touchpad left_handed enabled"])

    def test_apply_sends_one_request_and_records_failures(self):
        self.write({"version": 1, "keyboard": {"repeatDelay": 300, "repeatRate": 40}})
        sent = []

        def accept(path, kind, payload="", **_):
            sent.append((kind, payload))
            return [{"success": True} for _ in payload.split("; ")]
        state = {}
        with patch.object(session, "ipc", accept):
            self.assertTrue(session.apply_input_settings("backend.sock", self.profile, state))
        self.assertEqual(sent, [(0, "input type:keyboard repeat_delay 300; input type:keyboard repeat_rate 40")])
        self.assertEqual(state, {"input_applied": 2})

        def refuse(path, kind, payload="", **_):
            return [{"success": True}, {"success": False, "error": "nope"}]
        with patch.object(session, "ipc", refuse):
            self.assertFalse(session.apply_input_settings("backend.sock", self.profile, state))
        self.assertEqual(state["input_error"], "Sway refused an input setting")

        def unreachable(path, kind, payload="", **_):
            raise OSError("gone")
        with patch.object(session, "ipc", unreachable):
            self.assertFalse(session.apply_input_settings("backend.sock", self.profile, state))
        self.assertEqual(state["input_error"], "gone")

    def test_nothing_saved_sends_nothing(self):
        def fail(*_args, **_kwargs):
            raise AssertionError("no request expected")
        state = {"input_error": "old"}
        with patch.object(session, "ipc", fail):
            self.assertTrue(session.apply_input_settings("backend.sock", self.profile, state))
        self.assertEqual(state, {})


class EnergySettingsTests(unittest.TestCase):
    """The Energy Saver panel's energy.json and the swayidle command it gives."""
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="platinum-energy-test-")
        self.profile = Path(self.folder.name)

    def tearDown(self):
        self.folder.cleanup()

    def test_never_unless_chosen_and_in_range(self):
        self.assertEqual(session.saved_energy_settings(self.profile), {"displayOff": 0, "sleep": 0})
        for data in ({"version": 2, "displayOff": 5}, {"version": 1, "displayOff": 0.5, "sleep": 2},
                     {"version": 1, "displayOff": True, "sleep": 9999}, []):
            (self.profile / "energy.json").write_text(json.dumps(data))
            self.assertEqual(session.saved_energy_settings(self.profile), {"displayOff": 0, "sleep": 0})
        (self.profile / "energy.json").write_text(json.dumps({"version": 1, "displayOff": 10, "sleep": 30}))
        self.assertEqual(session.saved_energy_settings(self.profile), {"displayOff": 10, "sleep": 30})

    def test_the_idle_command_is_unchanged_without_energy_settings(self):
        plain = ["swayidle", "-C", "/dev/null", "-w", "before-sleep", "LOCK", "after-resume", "RESUME", "lock", "LOCK"]
        self.assertEqual(session.idle_command("LOCK", "RESUME"), plain)
        self.assertEqual(session.idle_command("LOCK", "RESUME", {"displayOff": 0, "sleep": 0}, "/run/x/sway.sock"), plain)

    def test_displays_off_and_sleep_become_swayidle_timeouts(self):
        command = session.idle_command("LOCK", "RESUME", {"displayOff": 10, "sleep": 30}, "/run/user/1000/a b/sway.sock")
        self.assertEqual(command[10:], [
            "timeout", "600", "swaymsg -s '/run/user/1000/a b/sway.sock' output '*' power off",
            "resume", "swaymsg -s '/run/user/1000/a b/sway.sock' output '*' power on",
            "timeout", "1800", "systemctl suspend"])
        # Before the lock settings: sleep still locks first.
        self.assertEqual(command[4:6], ["before-sleep", "LOCK"])


if __name__ == "__main__":
    unittest.main()
