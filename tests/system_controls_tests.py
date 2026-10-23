#!/usr/bin/env python3
"""Device mutations always use a fake command runner, never real hardware."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("controls", Path(__file__).resolve().parents[1] /
                                            "src/desktop/system_controls.py")
controls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(controls)


class ControlsTests(unittest.TestCase):
    def test_status_can_fail_each_service_without_guessing_state(self):
        # The internal panel's brightness is read from sysfs, not through run(): fake the active
        # display lookup too, or the result depends on whether the laptop is docked.
        with patch.object(controls, "run", side_effect=RuntimeError("offline")), \
                patch.object(controls, "active_connectors", side_effect=OSError("no displays")):
            state = controls.snapshot()
        self.assertEqual(set(state), {"volume", "brightness", "wifi", "bluetooth"})
        self.assertTrue(all(not item["available"] for item in state.values()))

    def test_audio_and_mute_are_bounded_fixed_arguments(self):
        with patch.object(controls, "run", return_value="Volume: 0.86 [MUTED]") as run:
            self.assertEqual(controls.volume(), dict(available=True, value=86, muted=True))
            controls.change("volume", 45)
            self.assertEqual(run.call_args.args[0], ["/usr/bin/wpctl", "set-volume", "--limit",
                             "1.0", "@DEFAULT_AUDIO_SINK@", "45%"])
            controls.change("mute", 0)
            self.assertEqual(run.call_args.args[0][-2:], ["@DEFAULT_AUDIO_SINK@", "0"])
            count = run.call_count
            for name, value in (("volume", 101), ("wifi", 2), ("brightness", -1),
                                ("shell", 1), ("volume", "45;id"), ("mute", True)):
                with self.assertRaises(ValueError): controls.change(name, value)
            self.assertEqual(count, run.call_count)

    def test_wifi_uses_iwd_when_networkmanager_is_not_running(self):
        def fake(args):
            if args[0].endswith("nmcli"): raise RuntimeError("NM absent")
            if "tree" in args: return "/\n/net/connman/iwd\n/net/connman/iwd/0\n/net/connman/iwd/0/1\n"
            if "get-property" in args: return "b true"
            return ""
        with patch.object(controls, "run", side_effect=fake) as run:
            self.assertTrue(controls.wifi()["enabled"])
            controls.change("wifi", 0)
            self.assertEqual(run.call_args.args[0], ["/usr/bin/busctl", "--system", "set-property",
                 "net.connman.iwd", "/net/connman/iwd/0", "net.connman.iwd.Adapter", "Powered", "b", "false"])

    def test_networkmanager_and_bluetooth_use_their_available_service(self):
        with patch.object(controls, "run", return_value="enabled") as run:
            controls.change("wifi", 1)
            self.assertEqual(run.call_args.args[0], ["/usr/bin/nmcli", "radio", "wifi", "on"])
        def fake(args):
            return "/org/bluez/hci0" if "tree" in args else "b false"
        with patch.object(controls, "run", side_effect=fake) as run:
            controls.change("bluetooth", 1)
            self.assertEqual(run.call_args.args[0], ["/usr/bin/busctl", "--system", "set-property",
                 "org.bluez", "/org/bluez/hci0", "org.bluez.Adapter1", "Powered", "b", "true"])

    def test_ddc_follows_the_only_active_external_connector(self):
        def fake(args):
            if "detect" in args:
                return "Display 1\n DRM connector: card1-DP-1\nDisplay 2\n DRM connector: card1-DP-2\n"
            if "getvcp" in args: return "VCP 10 C 25 100"
            return ""
        with patch.object(controls, "active_connectors", return_value=["DP-2"]), \
                patch.object(controls, "run", side_effect=fake) as run:
            self.assertEqual(controls.brightness()["target"], "2")
            controls.change("brightness", 0)
            self.assertEqual(run.call_args.args[0], ["/usr/bin/ddcutil", "setvcp", "10", "5", "--display", "2"])
        with patch.object(controls, "active_connectors", return_value=["DP-1", "DP-2"]), \
                patch.object(controls, "run") as run:
            with self.assertRaises(RuntimeError): controls.change("brightness", 50)
            run.assert_not_called()

    def test_disabled_laptop_panel_is_not_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, enabled in (("card1-eDP-1", "disabled"), ("card1-DP-2", "enabled")):
                path = root / name; path.mkdir()
                (path / "status").write_text("connected\n")
                (path / "enabled").write_text(enabled + "\n")
            self.assertEqual(controls.active_connectors(root), ["DP-2"])


if __name__ == "__main__": unittest.main()
