import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from session_support import adopt_previous_name, cleanup_runtime, write_bus_config


class SessionSafetyTests(unittest.TestCase):
    def test_private_bus_has_no_host_activation_or_includes(self):
        with tempfile.TemporaryDirectory(prefix="frankenstein-runtime.") as path:
            root = ET.parse(write_bus_config(path)).getroot()
            for name in ("servicedir", "standard_session_servicedirs", "include", "includedir"):
                self.assertEqual(root.findall(name), [])
            policy = root.find("policy")
            self.assertEqual(policy.find("deny").get("user"), "*")
            self.assertEqual(policy.find("allow").get("user"), pwd.getpwuid(os.getuid()).pw_name)
            self.assertEqual(root.find("auth").text, "EXTERNAL")

    def test_private_bus_keeps_the_stock_session_limits(self):
        with tempfile.TemporaryDirectory(prefix="frankenstein-runtime.") as path:
            limits = {limit.get("name"): limit.text for limit in ET.parse(write_bus_config(path)).getroot().findall("limit")}
        self.assertEqual(limits["max_replies_per_connection"], "50000")
        self.assertEqual(limits["max_match_rules_per_connection"], "50000")
        self.assertEqual(limits["max_names_per_connection"], "50000")
        self.assertEqual(limits["max_message_size"], "16777216")
        self.assertNotIn("reply_timeout", limits)

    @unittest.skipUnless(Path("/usr/share/dbus-1/session.conf").exists(), "no installed session bus configuration")
    def test_private_bus_limits_match_the_installed_session_bus(self):
        # Every limit the installed session bus sets, except the message size, which the private
        # bus deliberately keeps at 16 MiB.
        installed = {limit.get("name"): limit.text.strip()
                     for limit in ET.parse("/usr/share/dbus-1/session.conf").getroot().findall("limit")}
        self.assertIn("max_replies_per_connection", installed)
        with tempfile.TemporaryDirectory(prefix="frankenstein-runtime.") as path:
            limits = {limit.get("name"): limit.text for limit in ET.parse(write_bus_config(path)).getroot().findall("limit")}
        installed.pop("max_message_size", None)
        self.assertEqual({name: limits.get(name) for name in installed}, installed)
        self.assertEqual(set(limits) - set(installed), {"max_message_size"})

    @unittest.skipUnless(shutil.which("dbus-daemon"), "dbus-daemon is not installed")
    def test_unanswered_calls_do_not_exhaust_the_private_bus(self):
        # An application that never answers: each call holds a reply slot until it leaves.
        try:
            import dbus  # noqa: F401
        except ImportError:
            self.skipTest("dbus-python is not installed")
        service = (
            "import dbus, dbus.service, dbus.mainloop.glib, sys\n"
            "from gi.repository import GLib\n"
            "dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)\n"
            "bus = dbus.bus.BusConnection(sys.argv[1])\n"
            "class Silent(dbus.service.Object):\n"
            "    @dbus.service.method('org.frankenstein.Silent', async_callbacks=('ok', 'err'))\n"
            "    def Ignore(self, ok, err): pass\n"
            "name = dbus.service.BusName('org.frankenstein.Silent', bus)\n"
            "Silent(bus, '/s')\nprint('ready', flush=True)\nGLib.MainLoop().run()\n")
        caller = (
            "import dbus, dbus.mainloop.glib, sys\n"
            "from gi.repository import GLib\n"
            "dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)\n"
            "obj = dbus.bus.BusConnection(sys.argv[1]).get_object('org.frankenstein.Silent', '/s', introspect=False)\n"
            "errors = []\n"
            "for _ in range(300):\n"
            "    obj.Ignore(dbus_interface='org.frankenstein.Silent', reply_handler=lambda: None,\n"
            "               error_handler=lambda e: errors.append(e.get_dbus_name()), timeout=1)\n"
            "loop = GLib.MainLoop(); GLib.timeout_add(2500, loop.quit); loop.run()\n"
            "print(sorted(set(errors)), len(errors))\n")
        with tempfile.TemporaryDirectory(prefix="frankenstein-runtime.") as path:
            daemon = subprocess.Popen(["dbus-daemon", "--config-file", str(write_bus_config(path)),
                                       "--nofork", "--print-address"], stdout=subprocess.PIPE, text=True)
            try:
                address = daemon.stdout.readline().strip()
                silent = subprocess.Popen([sys.executable, "-c", service, address], stdout=subprocess.PIPE, text=True)
                try:
                    self.assertEqual(silent.stdout.readline().strip(), "ready")
                    result = subprocess.run([sys.executable, "-c", caller, address], capture_output=True,
                                            text=True, timeout=30)
                    # Every call times out on the caller's side; none is refused by the bus.
                    self.assertEqual(result.stdout.strip(), "['org.freedesktop.DBus.Error.NoReply'] 300",
                                     result.stderr)
                finally:
                    silent.kill()
                    silent.wait()
                    silent.stdout.close()
            finally:
                daemon.kill()
                daemon.wait()
                daemon.stdout.close()

    def test_cleanup_removes_only_expected_files(self):
        path = Path(tempfile.mkdtemp(prefix="frankenstein-runtime."))
        write_bus_config(path)
        (path / "frankenstein-qualification.lock").write_text("fixture")
        self.assertTrue(cleanup_runtime(path))
        self.assertFalse(path.exists())

    def test_cleanup_never_traverses_portal_directories_or_symlinks(self):
        with tempfile.TemporaryDirectory(prefix="frankenstein-runtime.") as path, tempfile.TemporaryDirectory() as outside:
            runtime = Path(path)
            (runtime / "doc").mkdir()
            (runtime / "doc" / "valuable").write_text("keep")
            marker = Path(outside) / "valuable"
            marker.write_text("keep")
            (runtime / "link").symlink_to(outside, target_is_directory=True)
            self.assertFalse(cleanup_runtime(runtime))
            self.assertEqual((runtime / "doc" / "valuable").read_text(), "keep")
            self.assertEqual(marker.read_text(), "keep")

    def test_the_platinum_dreams_folder_moves_once_and_leaves_a_link(self):
        with tempfile.TemporaryDirectory() as data:
            data = Path(data)
            (data / "platinum-dreams/profile").mkdir(parents=True)
            (data / "platinum-dreams/profile/finder.ini").write_text("[General]\ndiskName=Macintosh HD\n")
            self.assertTrue(adopt_previous_name(data))
            self.assertEqual((data / "platinum-reality/profile/finder.ini").read_text(),
                             "[General]\ndiskName=Macintosh HD\n")
            self.assertEqual(os.readlink(data / "platinum-dreams"), "platinum-reality")
            # Older releases still find everything through the link; a second call changes nothing.
            self.assertTrue((data / "platinum-dreams/profile/finder.ini").is_file())
            self.assertFalse(adopt_previous_name(data))
        with tempfile.TemporaryDirectory() as data:
            # Both exist: nothing is moved or replaced.
            data = Path(data)
            (data / "platinum-dreams").mkdir()
            (data / "platinum-reality").mkdir()
            self.assertFalse(adopt_previous_name(data))
            self.assertTrue((data / "platinum-dreams").is_dir() and not (data / "platinum-dreams").is_symlink())
            # Nothing to move.
            shutil.rmtree(data / "platinum-dreams")
            self.assertFalse(adopt_previous_name(data))

    def test_cleanup_removes_empty_owned_helper_directories(self):
        runtime = Path(tempfile.mkdtemp(prefix="frankenstein-runtime."))
        write_bus_config(runtime)
        (runtime / "cc-socks").mkdir()
        self.assertTrue(cleanup_runtime(runtime))
        self.assertFalse(runtime.exists())

    def test_launcher_preserves_success_when_a_helper_leaves_an_empty_directory(self):
        if not shutil.which("dbus-run-session"):
            self.skipTest("dbus-run-session is required for the installed launcher")
        project = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="platinum-launcher-check-") as temporary:
            root = Path(temporary)
            library = root / "lib/platinum-reality"
            library.mkdir(parents=True)
            launcher = root / "bin/platinum-reality"
            launcher.parent.mkdir()
            launcher.write_text((project / "packaging/platinum-reality.in").read_text().replace(
                "@PLATINUM_LIB_FROM_BIN@", "../lib/platinum-reality"))
            shutil.copyfile(project / "scripts/session_support.py", library / "session_support.py")
            helper = library / "frankenstein"
            helper.write_text("#!/usr/bin/env python3\nimport os\nfrom pathlib import Path\n"
                              "runtime = Path(os.environ['XDG_RUNTIME_DIR'])\n"
                              "(runtime / 'cc-socks').mkdir()\nprint('runtime=' + str(runtime))\n")
            helper.chmod(0o755)
            environment = os.environ.copy()
            environment["WAYLAND_DISPLAY"] = "launcher-fixture-no-display-needed"
            # Clean package builds (makepkg in a chroot or container) have no login session's runtime folder.
            parent_runtime = root / "parent-runtime"
            parent_runtime.mkdir(mode=0o700)
            environment["XDG_RUNTIME_DIR"] = str(parent_runtime)
            # Never the real data folder (the launcher looks after it).
            environment["XDG_DATA_HOME"] = str(root / "data")
            (root / "data/platinum-dreams").mkdir(parents=True)  # an explicit --profile leaves it alone
            result = subprocess.run([sys.executable, str(launcher), "--workspace", str(root / "workspace"),
                                     "--profile", str(root / "profile")], env=environment,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root / "data/platinum-dreams").is_dir() and not (root / "data/platinum-dreams").is_symlink())
            self.assertFalse((root / "data/platinum-reality").exists())
            runtime = Path(next(line.removeprefix("runtime=") for line in result.stdout.splitlines()
                                if line.startswith("runtime=")))
            self.assertFalse(runtime.exists())

    def test_cleanup_rejects_unrelated_directory(self):
        with tempfile.TemporaryDirectory(prefix="unrelated-") as path:
            marker = Path(path) / "valuable"
            marker.write_text("keep")
            with self.assertRaises(ValueError):
                cleanup_runtime(path)
            self.assertEqual(marker.read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
