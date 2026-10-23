"""Wallet configuration/lifetime boundaries, without a bus or any real wallet."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from session_support import (WALLET_SERVICES, cleanup_runtime, host_wallet_status,
                             wallet_login_requested, write_bus_config)


class WalletSupportTests(unittest.TestCase):
    def fixture(self, root):
        services = root / "source services"
        services.mkdir()
        for name in WALLET_SERVICES:
            (services / (name + ".service")).write_text(
                "[D-BUS Service]\nName=" + name + "\nExec=/usr/bin/true\n"
                "SystemdService=must-not-be-imported.service\n")
        (services / "org.example.Unrelated.service").write_text(
            "[D-BUS Service]\nName=org.example.Unrelated\nExec=/usr/bin/false\n")
        return services

    def test_only_a_physical_normal_settings_login_may_request_wallet_services(self):
        allowed = {"PLATINUM_LOGIN_SESSION": "1", "PLATINUM_WALLET_LOGIN": "1"}
        self.assertTrue(wallet_login_requested(allowed))
        self.assertFalse(wallet_login_requested(allowed, private_settings=True))
        for key in allowed:
            without = allowed.copy()
            without.pop(key)
            self.assertFalse(wallet_login_requested(without))
        for key in ("FRANKENSTEIN_ISOLATED", "FRANKENSTEIN_NATIVE_TEST"):
            self.assertFalse(wallet_login_requested({**allowed, key: "1"}))
        # An inherited availability flag alone cannot grant a new allowance.
        self.assertFalse(wallet_login_requested({"PLATINUM_WALLET_SERVICES": "1"}))

    def test_default_config_remains_activation_free_even_with_inherited_markers(self):
        with tempfile.TemporaryDirectory() as temporary, mock.patch.dict(os.environ, {
                "PLATINUM_LOGIN_SESSION": "1", "PLATINUM_WALLET_LOGIN": "1"}):
            root = ET.parse(write_bus_config(temporary)).getroot()
            self.assertEqual(root.findall("servicedir"), [])
            self.assertFalse((Path(temporary) / "wallet-services").exists())

    def test_explicit_wallet_config_contains_only_three_supervised_services(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sources = self.fixture(root)
            before = {p.name: p.read_bytes() for p in sources.iterdir()}
            runtime = root / "runtime"
            runtime.mkdir()
            tree = ET.parse(write_bus_config(runtime, wallet_supervisor="/usr/bin/true",
                                            service_directory=sources)).getroot()
            self.assertEqual([x.text for x in tree.findall("servicedir")], [str(runtime / "wallet-services")])
            for name in ("standard_session_servicedirs", "include", "includedir"):
                self.assertEqual(tree.findall(name), [])
            directory = runtime / "wallet-services"
            self.assertEqual({p.name for p in directory.iterdir()}, {n + ".service" for n in WALLET_SERVICES})
            for name in WALLET_SERVICES:
                text = (directory / (name + ".service")).read_text()
                self.assertIn("Name=" + name + "\n", text)
                self.assertNotIn("SystemdService", text)
                command = next(line[5:] for line in text.splitlines() if line.startswith("Exec="))
                self.assertEqual(shlex.split(command), ["/usr/bin/true", "--", "/usr/bin/true"])
            self.assertEqual(before, {p.name: p.read_bytes() for p in sources.iterdir()})

    def test_supervisor_paths_and_service_arguments_preserve_spaces(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sources = self.fixture(root)
            supervisor = root / "wallet supervisor"
            supervisor.write_text("#!/bin/sh\nexit 1\n")
            supervisor.chmod(0o700)
            name = WALLET_SERVICES[0]
            (sources / (name + ".service")).write_text(
                "[D-BUS Service]\nName=" + name + "\nExec=/usr/bin/true 'two words'\n")
            runtime = root / "runtime"
            runtime.mkdir()
            write_bus_config(runtime, wallet_supervisor=supervisor, service_directory=sources)
            text = (runtime / "wallet-services" / (name + ".service")).read_text()
            command = next(line[5:] for line in text.splitlines() if line.startswith("Exec="))
            self.assertEqual(shlex.split(command), [str(supervisor), "--", "/usr/bin/true", "two words"])

    def test_missing_or_mismatched_service_never_creates_a_partial_allowlist(self):
        for damage in ("missing", "wrong-name", "relative-exec", "missing-executable", "invalid"):
            with self.subTest(damage=damage), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                sources = self.fixture(root)
                path = sources / (WALLET_SERVICES[-1] + ".service")
                if damage == "missing":
                    path.unlink()
                else:
                    replacements = {
                        "wrong-name": "[D-BUS Service]\nName=org.example.Other\nExec=/usr/bin/true\n",
                        "relative-exec": "[D-BUS Service]\nName=" + WALLET_SERVICES[-1] + "\nExec=true\n",
                        "missing-executable": "[D-BUS Service]\nName=" + WALLET_SERVICES[-1] + "\nExec=/missing/wallet\n",
                        "invalid": "not a service\n",
                    }
                    path.write_text(replacements[damage])
                runtime = root / "runtime"
                runtime.mkdir()
                with self.assertRaises(ValueError):
                    write_bus_config(runtime, wallet_supervisor="/usr/bin/true", service_directory=sources)
                self.assertFalse((runtime / "wallet-services").exists())
                self.assertFalse((runtime / "bus.conf").exists())

    def test_missing_supervisor_cannot_enable_activation(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                write_bus_config(temporary, wallet_supervisor="/missing/supervisor")
            self.assertFalse((Path(temporary) / "wallet-services").exists())

    def test_cleanup_removes_known_wallet_files_but_retains_unexpected_contents(self):
        for unexpected in (False, True):
            with self.subTest(unexpected=unexpected), tempfile.TemporaryDirectory() as temporary:
                sources = self.fixture(Path(temporary))
                runtime = Path(tempfile.mkdtemp(prefix="frankenstein-runtime."))
                try:
                    write_bus_config(runtime, wallet_supervisor="/usr/bin/true", service_directory=sources)
                    if unexpected:
                        (runtime / "wallet-services" / "keep").write_text("keep")
                    self.assertEqual(cleanup_runtime(runtime), not unexpected)
                    if unexpected:
                        self.assertEqual((runtime / "wallet-services" / "keep").read_text(), "keep")
                        (runtime / "wallet-services" / "keep").unlink()
                        self.assertTrue(cleanup_runtime(runtime))
                finally:
                    if runtime.exists(): cleanup_runtime(runtime)

    def test_cleanup_does_not_follow_a_service_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            valuable = root / "keep"
            valuable.write_text("keep")
            runtime = Path(tempfile.mkdtemp(prefix="frankenstein-runtime."))
            directory = runtime / "wallet-services"
            directory.mkdir()
            link = directory / (WALLET_SERVICES[0] + ".service")
            link.symlink_to(valuable)
            try:
                self.assertFalse(cleanup_runtime(runtime))
                self.assertEqual(valuable.read_text(), "keep")
                self.assertTrue(link.is_symlink())
            finally:
                link.unlink()
                self.assertTrue(cleanup_runtime(runtime))

    def inspect(self, stdout="(['org.freedesktop.DBus'],)", returncode=0, side_effect=None):
        run = mock.Mock(side_effect=side_effect, return_value=subprocess.CompletedProcess([], returncode, stdout, ""))
        with tempfile.TemporaryDirectory() as temporary:
            status = host_wallet_status({"DBUS_SESSION_BUS_ADDRESS": "unix:path=/fixture-only/bus"},
                                        run=run, user_bus=Path(temporary) / "no-user-bus")
        if run.call_args:
            command = run.call_args.args[0]
            self.assertIn("org.freedesktop.DBus.ListNames", command)
            self.assertNotIn("StartServiceByName", command)
            self.assertEqual(run.call_args.kwargs["timeout"], 3)
        return status

    def test_existing_wallet_or_secret_service_disables_another_daemon(self):
        for name in (*WALLET_SERVICES, "org.freedesktop.secrets"):
            with self.subTest(name=name):
                self.assertEqual(self.inspect(repr(([name],))), "busy")
        self.assertEqual(self.inspect(), "idle")

    def test_host_bus_errors_are_unknown_never_treated_as_idle(self):
        for text in ("", "not gvariant", "('not a list',)", "([42],)", "([], [],)"):
            with self.subTest(text=text):
                self.assertEqual(self.inspect(text), "unknown")
        self.assertEqual(self.inspect(returncode=1), "unknown")
        self.assertEqual(self.inspect(side_effect=PermissionError("denied")), "unknown")
        self.assertEqual(self.inspect(side_effect=subprocess.TimeoutExpired("fixture", 3)), "unknown")

    def test_absent_host_bus_does_not_run_an_activation_probe(self):
        run = mock.Mock()
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(host_wallet_status({}, run=run, user_bus=Path(temporary) / "absent"), "idle")
        run.assert_not_called()

    def test_supervisor_cannot_start_a_program_without_service_activation(self):
        project = Path(__file__).resolve().parents[1]
        supervisor = Path(os.environ.get("FRANKENSTEIN_BUILD_DIR", str(project / "build"))) / "platinum-wallet-service"
        self.assertTrue(supervisor.is_file(), "Build platinum-wallet-service before testing")
        with tempfile.TemporaryDirectory() as temporary:
            marker = Path(temporary) / "must-not-exist"
            environment = os.environ.copy()
            environment.pop("DBUS_STARTER_ADDRESS", None)
            for arguments in (["--", "/usr/bin/touch", str(marker)], ["--", "relative-program"], []):
                result = subprocess.run([str(supervisor), *arguments], env=environment,
                                        capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2, result.stderr)
            self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
