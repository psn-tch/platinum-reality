#!/usr/bin/env python3
"""platinum-login-switch.py on disposable homes: only the known tty1 block changes, and back."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/platinum-login-switch.py'


class LoginSwitchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.home = Path(self.directory.name)
        self.root = self.home / '.local/share/platinum-dreams-native'
        self.root.mkdir(parents=True)
        self.selector = self.root / 'login-session.py'
        self.selector.write_text('parser.add_argument("--graphical")\n')
        self.profile = self.home / '.bash_profile'
        self.original = ('#\n# ~/.bash_profile\n#\n\n[[ -f ~/.bashrc ]] && . ~/.bashrc\n\n'
                         '[ -t 0 ] && echo 3 | sudo tee /sys/class/graphics/fbcon/rotate > /dev/null\n\n'
                         'if [ -z "${DISPLAY}" ] && [ "${XDG_VTNR}" -eq 1 ] && [ "$(tty)" = /dev/tty1 ]; then\n'
                         '\tmkdir -p ~/.cache\n'
                         f'\t~/.fingerprint-gate.sh && exec /usr/bin/python3 {self.selector} --login\n'
                         'fi \n')
        self.profile.write_text(self.original)

    def tearDown(self):
        self.directory.cleanup()

    def switch(self, state):
        return subprocess.run(['python3', '-I', str(SCRIPT), state], env=dict(os.environ, HOME=str(self.home)),
                              capture_output=True, text=True)

    def test_on_replaces_only_the_tty1_gate_before_the_sudo_line_and_off_restores_it(self):
        self.assertEqual(self.switch('on').returncode, 0)
        new = self.profile.read_text()
        self.assertNotIn('fingerprint-gate', new)
        self.assertLess(new.index('--login --graphical'), new.index('sudo tee'))
        self.assertIn('\texit 1\n', new)
        self.assertEqual(subprocess.run(['bash', '-n', str(self.profile)]).returncode, 0)
        self.assertNotEqual(self.switch('on').returncode, 0)  # Never twice.
        self.assertEqual(self.switch('off').returncode, 0)
        self.assertEqual(self.profile.read_text(), self.original)

    def test_unknown_profile_or_old_release_changes_nothing(self):
        self.profile.write_text(self.original + 'echo something else\n')
        self.assertNotEqual(self.switch('on').returncode, 0)
        self.profile.write_text(self.original)
        self.selector.write_text('no Log In here\n')
        self.assertNotEqual(self.switch('on').returncode, 0)
        self.assertEqual(self.profile.read_text(), self.original)
        self.assertFalse((self.root / 'bash_profile.before-platinum-login').exists())

    def test_off_refuses_a_profile_changed_since(self):
        self.assertEqual(self.switch('on').returncode, 0)
        self.profile.write_text('edited by hand\n')
        self.assertNotEqual(self.switch('off').returncode, 0)
        self.assertEqual(self.profile.read_text(), 'edited by hand\n')


if __name__ == '__main__':
    unittest.main()
