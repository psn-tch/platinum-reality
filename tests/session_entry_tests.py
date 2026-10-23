"""The login-screen session command (packaging/platinum-reality-session.in), built: what it starts and
what it keeps. Nothing is started; PLATINUM_SESSION_PRINT_ONLY shows the command instead."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
BUILT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None


class SessionEntryTests(unittest.TestCase):
    def run_entry(self, state, *extra):
        environment = {'PATH': '/usr/bin:/bin', 'HOME': str(state.parent), 'XDG_STATE_HOME': str(state),
                       'PLATINUM_SESSION_PRINT_ONLY': '1'}
        result = subprocess.run([sys.executable, str(BUILT), *extra], env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.rstrip('\n').split('\0')

    def test_it_starts_the_supervisor_with_the_installed_desktop_and_a_new_log_folder(self):
        with tempfile.TemporaryDirectory() as home:
            state = Path(home) / 'state'
            command = self.run_entry(state, '--no-kde-output-settings')
            self.assertTrue(command[2].endswith('standalone-session.py'), command)
            self.assertEqual(command[3], '--launcher')
            self.assertEqual(Path(command[4]).name, 'platinum-reality')
            self.assertEqual(Path(command[4]).parent, BUILT.parent)
            self.assertEqual(command[5], '--evidence')
            run = Path(command[6])
            self.assertEqual(run.parent, state / 'platinum-reality/session-runs')
            self.assertFalse(run.exists())  # The supervisor creates it (and refuses an existing one).
            self.assertEqual(command[7:], ['--primary-output', 'auto', '--single-output', '--no-kde-output-settings'])

    def test_old_session_logs_are_pruned_but_nothing_else(self):
        with tempfile.TemporaryDirectory() as home:
            state = Path(home) / 'state'
            runs = state / 'platinum-reality/session-runs'
            runs.mkdir(parents=True)
            for day in range(25):
                (runs / f'202610{day + 1:02d}T000000Z-1').mkdir()
            (runs / 'notes').mkdir()
            elsewhere = Path(home) / 'elsewhere'
            elsewhere.mkdir()
            (runs / '20990101T000000Z-1').symlink_to(elsewhere)
            self.run_entry(state)
            kept = sorted(p.name for p in runs.iterdir() if not p.is_symlink() and p.name[:8].isdigit())
            self.assertEqual(len(kept), 19)  # With the session starting now: the newest 20.
            self.assertEqual(kept[0], '20261007T000000Z-1')
            self.assertTrue((runs / 'notes').is_dir())
            self.assertTrue(elsewhere.is_dir())

    def test_a_relative_state_folder_is_refused(self):
        environment = {'PATH': '/usr/bin:/bin', 'HOME': '/nonexistent', 'XDG_STATE_HOME': 'relative',
                       'PLATINUM_SESSION_PRINT_ONLY': '1'}
        result = subprocess.run([sys.executable, str(BUILT)], env=environment, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
