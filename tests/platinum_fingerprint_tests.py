#!/usr/bin/env python3
"""Fingerprint login's PAM edits, on copies of Arch's own PAM files in a throwaway root."""
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('platinum_fingerprint', HERE.parent / 'scripts/platinum_fingerprint.py')
fingerprint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fingerprint)
FIXTURES = HERE / 'fixtures/pam-arch'


class FingerprintLoginTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        (self.root / 'etc/pam.d').mkdir(parents=True)
        (self.root / 'usr/lib/pam.d').mkdir(parents=True)
        (self.root / 'usr/lib/security').mkdir(parents=True)
        for name in ('sddm', 'swaylock', 'sudo'):
            shutil.copy(FIXTURES / name, self.root / 'etc/pam.d' / name)
        shutil.copy(FIXTURES / 'polkit-1.vendor', self.root / 'usr/lib/pam.d/polkit-1')
        (self.root / fingerprint.MODULE).write_bytes(b'')
        (self.root / fingerprint.THEME).mkdir(parents=True)
        self.original = self.snapshot()
        self.ticks = 0

    def tearDown(self):
        self.folder.cleanup()

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in sorted((self.root / 'etc/pam.d').iterdir())}

    def pam(self, name):
        return (self.root / 'etc/pam.d' / name).read_text()

    def apply(self, login, sudo):
        self.ticks += 1
        return fingerprint.apply(self.root, login, sudo, now=datetime(2026, 11, 10, 20, 0, self.ticks,
                                                                     tzinfo=timezone.utc))

    @staticmethod
    def auth(text):
        return [line.split('#')[0].split() for line in text.splitlines()
                if line.strip() and not line.startswith('#') and line.split()[0] in ('auth', '-auth')]

    def test_login_puts_the_finger_after_a_typed_password_and_before_the_usual_stack(self):
        self.assertIsNotNone(self.apply(True, False))
        sddm = self.auth(self.pam('sddm'))
        self.assertEqual(sddm[0], ['-auth', '[success=1', 'default=ignore]', 'pam_unix.so', 'try_first_pass'])
        self.assertEqual(sddm[1], ['-auth', 'sufficient', 'pam_fprintd.so', 'max-tries=3', 'timeout=10'])
        # Everything that was there still follows, in order: the keyring still gets a typed password.
        self.assertEqual(sddm[2:], self.auth((FIXTURES / 'sddm').read_text()))
        self.assertTrue(self.pam('sddm').startswith('#%PAM-1.0\n'))
        self.assertEqual(self.auth(self.pam('swaylock'))[:2], sddm[:2])
        # Password boxes: the finger first. Our own copy of polkit's file, with its header.
        polkit = self.pam('polkit-1')
        self.assertTrue(polkit.startswith(fingerprint.COPIED.format('usr/lib/pam.d/polkit-1') + '\n#%PAM-1.0\n'))
        self.assertEqual(self.auth(polkit), [['-auth', 'sufficient', 'pam_fprintd.so', 'max-tries=3', 'timeout=10'],
                                             ['auth', 'include', 'system-auth']])
        self.assertEqual(self.pam('sudo'), (FIXTURES / 'sudo').read_text())
        self.assertEqual(fingerprint.status(self.root), {'available': True, 'login': True, 'sudo': False})

    def test_off_gives_back_every_file_byte_for_byte(self):
        self.apply(True, True)
        self.assertEqual(fingerprint.status(self.root)['sudo'], True)
        self.assertNotEqual(self.snapshot(), self.original)
        self.apply(False, False)
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.root / 'etc/pam.d/polkit-1').exists())
        self.assertEqual(fingerprint.status(self.root), {'available': True, 'login': False, 'sudo': False})

    def test_turning_on_twice_changes_nothing_the_second_time(self):
        self.apply(True, False)
        once = self.snapshot()
        self.assertIsNone(self.apply(True, False))
        self.assertEqual(self.snapshot(), once)
        self.apply(True, True)
        self.apply(True, False)
        self.assertEqual(self.snapshot(), once)

    def test_backups_hold_the_files_before_each_change_and_only_ten_are_kept(self):
        backup = self.apply(True, False)
        self.assertEqual((backup / 'sddm').read_bytes(), self.original['etc/pam.d/sddm'])
        self.assertFalse((backup / 'polkit-1').exists())  # there was no /etc copy before
        self.assertEqual(backup.stat().st_mode & 0o777, 0o700)
        for _ in range(12):
            self.apply(False, False)
            self.apply(True, False)
        self.assertEqual(len(list((self.root / fingerprint.BACKUPS).iterdir())), fingerprint.KEEP_BACKUPS)

    def test_without_fprintd_nothing_turns_on_but_off_still_works(self):
        self.apply(True, True)
        (self.root / fingerprint.MODULE).unlink()
        with self.assertRaises(fingerprint.FingerprintError):
            self.apply(True, False)
        self.assertEqual(fingerprint.status(self.root)['available'], False)
        self.apply(False, False)
        self.assertEqual(self.snapshot(), self.original)

    def test_an_administrators_own_polkit_file_is_edited_and_kept(self):
        own = '#%PAM-1.0\n# mine\nauth include system-auth\naccount include system-auth\n'
        (self.root / 'etc/pam.d/polkit-1').write_text(own)
        self.apply(True, False)
        self.assertEqual(self.pam('polkit-1'), '#%PAM-1.0\n# mine\n' + fingerprint.APPROVE[0] + '\n'
                         'auth include system-auth\naccount include system-auth\n')
        self.apply(False, False)
        self.assertEqual(self.pam('polkit-1'), own)

    def test_odd_files_are_refused_before_anything_changes(self):
        (self.root / 'etc/pam.d/swaylock').unlink()
        (self.root / 'etc/pam.d/swaylock').symlink_to('sudo')
        with self.assertRaises(fingerprint.FingerprintError):
            self.apply(True, False)
        (self.root / 'etc/pam.d/swaylock').unlink()
        (self.root / 'etc/pam.d/swaylock').write_text('# nothing but comments\n')
        with self.assertRaises(fingerprint.FingerprintError):
            self.apply(True, False)
        self.assertEqual(self.pam('sddm'), self.original['etc/pam.d/sddm'].decode())
        self.assertFalse((self.root / fingerprint.BACKUPS).exists())

    def test_services_that_are_not_installed_are_left_alone(self):
        (self.root / 'etc/pam.d/sddm').unlink()
        (self.root / 'etc/pam.d/sudo').unlink()
        self.apply(True, True)
        self.assertFalse((self.root / 'etc/pam.d/sddm').exists())
        self.assertFalse((self.root / 'etc/pam.d/sudo').exists())
        self.assertTrue(fingerprint.status(self.root)['login'])

    def test_a_file_without_a_final_newline(self):
        (self.root / 'etc/pam.d/swaylock').write_text('auth include login')
        self.apply(True, False)
        self.assertEqual(self.pam('swaylock'), '\n'.join(fingerprint.LOGIN) + '\nauth include login')
        self.apply(False, False)
        self.assertEqual(self.pam('swaylock'), 'auth include login')

    def test_the_sddm_theme_learns_it_through_its_own_override_file(self):
        user = self.root / fingerprint.THEME / 'theme.conf.user'
        self.apply(True, False)
        self.assertEqual(user.read_text(), '[General]\n' + fingerprint.MARK + '\nfingerprint=true\n')
        self.apply(False, True)
        self.assertFalse(user.exists())
        # Someone's own settings there (KDE's SDDM settings write this file too) are kept.
        user.write_text('[General]\nbackground=/srv/picture.png\n')
        self.apply(True, False)
        self.assertEqual(user.read_text(), '[General]\n' + fingerprint.MARK + '\nfingerprint=true\n'
                         'background=/srv/picture.png\n')
        self.apply(True, False)
        self.assertEqual(user.read_text().count('fingerprint=true'), 1)
        self.apply(False, False)
        self.assertEqual(user.read_text(), '[General]\nbackground=/srv/picture.png\n')
        self.assertEqual(fingerprint.theme_settings('', False), '')
        self.assertEqual(fingerprint.theme_settings('[Other]\nx=1\n', True),
                         '[Other]\nx=1\n[General]\n' + fingerprint.MARK + '\nfingerprint=true\n')

    def test_mode_is_kept(self):
        os.chmod(self.root / 'etc/pam.d/sudo', 0o640)
        self.apply(False, True)
        self.assertEqual((self.root / 'etc/pam.d/sudo').stat().st_mode & 0o777, 0o640)
        self.assertEqual((self.root / 'etc/pam.d/polkit-1').exists(), False)

    def test_the_helper_reports_status_and_refuses_changes_without_root(self):
        helper = HERE.parent / 'scripts/platinum-fingerprint-login'
        environment = {'PATH': '/usr/bin:/bin', 'PYTHONPATH': ''}
        run = subprocess.run([sys.executable, '-I', str(helper), 'status'], capture_output=True, text=True,
                             env=environment)
        # The installed helper imports from /usr/lib/platinum-reality; here it may not exist.
        if run.returncode == 0:
            self.assertEqual(set(json.loads(run.stdout)), {'available', 'login', 'sudo'})
        if os.geteuid() != 0:
            for arguments in (['set', 'on', 'off'], ['install-support']):
                run = subprocess.run([sys.executable, '-I', str(helper), *arguments], capture_output=True,
                                     text=True, env=environment)
                if 'No module named' not in run.stderr:
                    self.assertEqual(run.returncode, 1)
                    self.assertIn('administrator', json.loads(run.stdout)['error'])
        run = subprocess.run([sys.executable, '-I', str(helper), 'set', 'maybe', 'off'], capture_output=True,
                             text=True, env=environment)
        if 'No module named' not in run.stderr:
            self.assertEqual(run.returncode, 2)


if __name__ == '__main__':
    unittest.main()
