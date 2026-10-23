import importlib.util
import io
import os
from types import SimpleNamespace
import json
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "scripts" / "setup-fresh-arch.py"
spec = importlib.util.spec_from_file_location("installer", SOURCE)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallPlanTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.package = Path(self.directory.name) / "reviewed.pkg.tar"

    def fixture(self, name="platinum-reality", architecture="x86_64", dependency="qt6-base>=6.12"):
        contents = (f"pkgname = {name}\npkgver = 0.1.0-1\narch = {architecture}\n"
                    f"depend = {dependency}\ndepend = polkit\n").encode()
        with tarfile.open(self.package, "w") as archive:
            member = tarfile.TarInfo(".PKGINFO"); member.size = len(contents)
            archive.addfile(member, io.BytesIO(contents))
        return hashlib.sha256(self.package.read_bytes()).hexdigest()

    def test_hash_checked_before_archive_parser(self):
        self.fixture()
        with patch.object(installer.subprocess, "run") as execute:
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                installer.build_plan(self.package, "0"*64, "iwd-networkd")
            execute.assert_not_called()

    def test_dependency_plan_and_no_autologin_or_reboot(self):
        plan = installer.build_plan(self.package, self.fixture(), "iwd-networkd")
        self.assertIn("qt6-base", plan["packages"])
        self.assertIn("polkit", plan["packages"])
        self.assertIn("systemd-resolved.service", plan["services_enabled_for_next_boot"])
        self.assertFalse(plan["autologin"])
        self.assertEqual(plan["sddm_preselected_session"], installer.SDDM_SESSION)
        self.assertFalse(plan["reboot_automatically"])

    def test_preserve_network_has_no_network_files_or_service_changes(self):
        plan = installer.build_plan(self.package, self.fixture(), "preserve-wired")
        self.assertEqual(plan["files"], {})
        self.assertNotIn("iwd.service", plan["services_enabled_for_next_boot"])
        self.assertNotIn("systemd-networkd.service", plan["services_enabled_for_next_boot"])

    def test_rejects_other_package(self):
        digest = self.fixture(name="some-other-package")
        with self.assertRaisesRegex(ValueError, "platinum-reality"):
            installer.build_plan(self.package, digest, "iwd-networkd")

    def test_rejects_other_architecture(self):
        digest = self.fixture(architecture="aarch64")
        with self.assertRaisesRegex(ValueError, "x86_64"):
            installer.build_plan(self.package, digest, "iwd-networkd")

    def test_rejects_dependency_shell_syntax(self):
        digest = self.fixture(dependency="qt6-base;touch /tmp/not-allowed")
        with self.assertRaisesRegex(ValueError, "dependency syntax"):
            installer.build_plan(self.package, digest, "iwd-networkd")

    def test_physical_host_guard_precedes_account_lookup_and_changes(self):
        with patch.object(installer.os, "geteuid", return_value=0), \
             patch.object(installer.Path, "is_file", return_value=True), \
             patch.object(installer.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "none\n")), \
             patch.object(installer.pwd, "getpwnam") as lookup:
            with self.assertRaisesRegex(ValueError, "refuses physical"):
                installer.target_checks({}, "tester")
            lookup.assert_not_called()

    def test_cli_is_plan_only_by_default(self):
        digest = self.fixture()
        result = subprocess.run(["python3", str(SOURCE), "--package", str(self.package), "--sha256", digest],
            check=True, capture_output=True, text=True,
            env={"PATH": "/usr/bin:/bin", "HOME": self.directory.name,
                 "XDG_DATA_HOME": self.directory.name + "/data", "LANG": "C.UTF-8"})
        self.assertFalse(json.loads(result.stdout)["autologin"])
        self.assertEqual(result.stderr, "")

    def test_staged_copy_survives_mutation_of_the_original_path(self):
        digest = self.fixture()
        with installer.staged_package(self.package, digest) as staged:
            self.package.write_bytes(b"changed after review")
            info, _ = installer.package_info(staged, digest)
            self.assertEqual(info["pkgname"], ["platinum-reality"])
            self.assertEqual(staged.stat().st_mode & 0o777, 0o600)
            self.assertEqual(staged.parent.stat().st_mode & 0o777, 0o700)
        self.assertFalse(staged.exists())

    def test_staging_rejects_changed_bytes_before_yielding(self):
        digest = self.fixture()
        self.package.write_bytes(b"changed before copy")
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            with installer.staged_package(self.package, digest):
                self.fail("A changed package must never reach pacman")

    def test_the_reviewed_package_is_always_installed(self):
        # pacman -U --needed skips a same-version package with different bytes.
        source = Path(installer.__file__).read_text()
        self.assertIn('["pacman", "-U", "--noconfirm", "--", str(package)]', source)
        self.assertNotIn('"-U", "--needed"', source)

    def test_password_must_be_present_and_not_locked(self):
        installer.require_local_password("tester:$6$synthetic:1:2:3\n", "tester")
        for record in ["", "other:$6$synthetic:1\n", "tester::1\n", "tester:!:1\n", "tester:*:1\n"]:
            with self.subTest(record=record), self.assertRaisesRegex(ValueError, "local login password"):
                installer.require_local_password(record, "tester")

    def test_container_on_a_virtual_host_is_also_rejected(self):
        with patch.object(installer.os, "geteuid", return_value=0), \
             patch.object(installer.Path, "is_file", return_value=True), \
             patch.object(installer.subprocess, "run", side_effect=[
                 subprocess.CompletedProcess([], 0, "kvm\n"),
                 subprocess.CompletedProcess([], 0, "docker\n")]), \
             patch.object(installer.pwd, "getpwnam") as lookup:
            with self.assertRaisesRegex(ValueError, "refuses containers"):
                installer.target_checks({}, "tester")
            lookup.assert_not_called()

    def test_staging_ignores_untrusted_tmpdir(self):
        digest = self.fixture()
        with patch.dict("os.environ", {"TMPDIR": self.directory.name}):
            with installer.staged_package(self.package, digest) as staged:
                self.assertEqual(staged.parent.parent, Path("/var/tmp"))

    def test_metadata_plan_is_rechecked_before_any_root_command(self):
        plan = installer.build_plan(self.package, self.fixture(), "iwd-networkd")
        plan["packages"].append("unexpected-package")
        execute = subprocess.run
        with patch.object(installer, "target_checks"), \
             patch.object(installer.subprocess, "run", wraps=execute) as run:
            with self.assertRaisesRegex(ValueError, "verified private copy"):
                installer.apply(plan, "tester")
            self.assertTrue(run.call_args_list)
            for call in run.call_args_list:
                self.assertEqual(call.args[0][0], "bsdtar")

    def test_sddm_active_or_unknown_state_is_refused(self):
        for state, code in [("active", 0), ("activating", 0), ("deactivating", 0), ("", 1)]:
            with self.subTest(state=state), patch.object(installer.subprocess, "run",
                    return_value=subprocess.CompletedProcess([], code, state+"\n")):
                with self.assertRaisesRegex(ValueError, "SDDM must be stopped"):
                    installer.require_sddm_stopped()
        for state in ("inactive", "failed"):
            with patch.object(installer.subprocess, "run",
                    return_value=subprocess.CompletedProcess([], 0, state+"\n")):
                installer.require_sddm_stopped()

    def test_sddm_selection_preserves_other_values_and_allows_password_login(self):
        old = "[Last]\nSession=/usr/share/wayland-sessions/plasma.desktop\nUser=tester\n\n[Other]\nNote=100% unchanged\n"
        value = installer.updated_sddm_state(old, installer.SDDM_SESSION)
        self.assertIn("Session="+installer.SDDM_SESSION, value)
        self.assertIn("User=tester", value)
        self.assertIn("Note=100% unchanged", value)
        self.assertNotIn("Autologin", value)
        self.assertEqual(installer.updated_sddm_state(value, installer.SDDM_SESSION), value)
        self.assertIn("[Last]", installer.updated_sddm_state("", installer.SDDM_SESSION))
        with self.assertRaisesRegex(ValueError, "parse SDDM"):
            installer.updated_sddm_state("[Last]\nSession=a\nSession=b\n", installer.SDDM_SESSION)

    def test_sddm_selection_atomic_owner_mode_and_symlink_refusal(self):
        home = Path(self.directory.name)/"sddm"
        home.mkdir(mode=0o700)
        session = Path(self.directory.name)/"session.desktop"
        session.write_text("[Desktop Entry]\nName=Fixture\n")
        account = SimpleNamespace(pw_dir=str(home), pw_uid=os.getuid(), pw_gid=os.getgid())
        with patch.object(installer, "require_sddm_stopped"):
            previous_umask = os.umask(0o777)
            try:
                installer.preselect_sddm_session(account, str(session))
            finally:
                os.umask(previous_umask)
            state = home/"state.conf"
            self.assertEqual(state.stat().st_mode & 0o777, 0o600)
            self.assertEqual(state.stat().st_uid, os.getuid())
            first = state.read_bytes()
            installer.preselect_sddm_session(account, str(session))
            self.assertEqual(state.read_bytes(), first)
            state.unlink()
            outside = Path(self.directory.name)/"outside"
            outside.write_text("must survive")
            state.symlink_to(outside)
            with self.assertRaises(OSError):
                installer.preselect_sddm_session(account, str(session))
            self.assertEqual(outside.read_text(), "must survive")
            self.assertEqual(sorted(p.name for p in home.iterdir()), ["state.conf"])

    def test_sddm_failure_keeps_previous_state(self):
        home = Path(self.directory.name)/"sddm"
        home.mkdir(mode=0o700)
        state = home/"state.conf"
        state.write_text("[Last]\nUser=tester\n")
        session = Path(self.directory.name)/"session.desktop"
        session.touch()
        account = SimpleNamespace(pw_dir=str(home), pw_uid=os.getuid(), pw_gid=os.getgid())
        with patch.object(installer, "require_sddm_stopped"), \
             patch.object(installer.os, "replace", side_effect=OSError("fixture failure")):
            with self.assertRaises(OSError):
                installer.preselect_sddm_session(account, str(session))
        self.assertEqual(state.read_text(), "[Last]\nUser=tester\n")
        self.assertEqual(sorted(p.name for p in home.iterdir()), ["state.conf"])

    def test_apply_needs_confirmation_and_an_explicit_account(self):
        digest = self.fixture()
        base = ["python3", str(SOURCE), "--package", str(self.package), "--sha256", digest, "--apply"]
        environment = {"PATH": "/usr/bin:/bin", "HOME": self.directory.name,
                       "XDG_DATA_HOME": self.directory.name+"/data", "LANG": "C.UTF-8"}
        for extra, error in [([], "--confirm-fresh-arch"), (["--confirm-fresh-arch"], "explicit --user")]:
            result = subprocess.run(base+extra, capture_output=True, text=True, env=environment)
            self.assertEqual(result.returncode, 2)
            self.assertIn(error, result.stderr)


if __name__ == "__main__":
    unittest.main()
