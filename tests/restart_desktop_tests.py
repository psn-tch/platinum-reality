"""Restart Desktop in the session supervisor (Lily Hewitt, 2026-11-12): a desktop that exits with
RESTART_DESKTOP_EXIT is replaced by a new one in the same session, without Log In; any other exit
ends the session as before. Runs scripts/standalone-session.py nested in an owned headless Sway
(private runtime directory, no input devices), with a stand-in desktop: native-probe full screen.
Never touches a user's session.
"""
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

PROJECT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('FRANKENSTEIN_BUILD_DIR', PROJECT / 'build'))

# The stand-in desktop: full screen until `exit-code` appears in its folder, then it exits with the
# code written there (once). Each start is counted in `starts`.
LAUNCHER = """#!/usr/bin/env python3
import os, signal, subprocess, sys, time
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, 'starts'), 'a') as starts:
    starts.write('%d %s\\n' % (os.getpid(), os.environ.get('PLATINUM_DESKTOP_RESTART', '-')))
# native-probe registers on a session bus; like the real launcher, each desktop has its own.
probe = subprocess.Popen(['dbus-run-session', '--', {probe!r}, 'Desktop', '--start-fullscreen'],
                         start_new_session=True)
order = os.path.join(here, 'exit-code')
while probe.poll() is None:
    if os.path.exists(order):
        code = int(open(order).read())
        os.remove(order)
        os.killpg(probe.pid, signal.SIGTERM)
        probe.wait(5)
        sys.exit(code)
    time.sleep(0.05)
sys.exit(probe.returncode)
"""


def wait(condition, timeout, step=0.05):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = condition()
        if value:
            return value
        time.sleep(step)
    return condition()


class RestartDesktopTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for tool in ('sway', 'swaymsg', 'swaylock', 'swayidle'):
            if not shutil.which(tool):
                raise unittest.SkipTest(tool + ' is not installed')
        if not (BUILD / 'native-probe').is_file():
            raise unittest.SkipTest('native-probe is not built')
        # KEEP_RESTART_TEST=1 keeps the folder (its evidence) for a look afterwards.
        cls.directory = (tempfile.TemporaryDirectory() if not os.environ.get('KEEP_RESTART_TEST')
                         else __import__('types').SimpleNamespace(name=tempfile.mkdtemp(), cleanup=lambda: None))
        root = Path(cls.directory.name)
        cls.runtime = root / 'runtime'
        cls.runtime.mkdir(mode=0o700)
        (root / 'sway.conf').write_text('xwayland disable\nswaybg_command -\nswaynag_command -\n'
                                        'output HEADLESS-1 resolution 1024x700 position 0 0\n')
        environment = {k: v for k, v in os.environ.items()
                       if k not in ('WAYLAND_DISPLAY', 'DISPLAY', 'SWAYSOCK', 'I3SOCK', 'XDG_SESSION_ID',
                                    'WAYLAND_SOCKET', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_STYLE_OVERRIDE')}
        environment.update(XDG_RUNTIME_DIR=str(cls.runtime), WLR_BACKENDS='headless', WLR_HEADLESS_OUTPUTS='1',
                           WLR_LIBINPUT_NO_DEVICES='1', WLR_RENDERER='pixman')
        cls.log = open(root / 'sway.txt', 'wb')
        cls.sway = subprocess.Popen(['sway', '--config', str(root / 'sway.conf')], env=environment,
                                    stdin=subprocess.DEVNULL, stdout=cls.log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
        if not wait(lambda: [p for p in cls.runtime.glob('wayland-*') if p.is_socket()], 15) or cls.sway.poll() is not None:
            cls.tearDownClass()
            raise RuntimeError('Owned headless Sway did not start; see ' + str(root / 'sway.txt'))
        display = [p for p in cls.runtime.glob('wayland-*') if p.is_socket()][0]
        cls.environment = dict(environment, WAYLAND_DISPLAY=str(display), QT_QUICK_BACKEND='software',
                               FRANKENSTEIN_NATIVE_TEST='1',
                               HOME=str(root / 'home'), XDG_CONFIG_HOME=str(root / 'config'),
                               XDG_DATA_HOME=str(root / 'data'), XDG_STATE_HOME=str(root / 'state'),
                               XDG_CACHE_HOME=str(root / 'cache'))
        for key in ('WLR_BACKENDS', 'WLR_HEADLESS_OUTPUTS', 'WLR_LIBINPUT_NO_DEVICES', 'WLR_RENDERER'):
            cls.environment.pop(key)
        for part in ('home', 'config', 'data', 'state', 'cache'):
            (root / part).mkdir()

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, 'sway', None) and cls.sway.poll() is None:
            os.killpg(cls.sway.pid, signal.SIGTERM)
            cls.sway.wait(10)
        if getattr(cls, 'log', None):
            cls.log.close()
        cls.directory.cleanup()

    def supervise(self, name):
        stage = Path(self.directory.name) / name
        stage.mkdir()
        launcher = stage / 'platinum-reality'
        launcher.write_text(LAUNCHER.format(probe=str(BUILD / 'native-probe')))
        launcher.chmod(0o755)
        evidence = stage / 'evidence'
        child = subprocess.Popen([sys.executable, '-B', str(PROJECT / 'scripts/standalone-session.py'),
                                  '--nested-check', '--launcher', str(launcher), '--evidence', str(evidence),
                                  '--profile', str(stage / 'profile')],
                                 env=self.environment, stdin=subprocess.DEVNULL,
                                 stdout=(stage / 'supervisor-out.txt').open('wb'), stderr=subprocess.STDOUT)
        self.addCleanup(lambda: child.poll() is None and (child.send_signal(signal.SIGTERM), child.wait(20)))
        return child, stage, evidence / 'session.json'

    @staticmethod
    def read(path):
        try:
            return json.loads(path.read_text())
        except (OSError, ValueError):
            return {}

    def test_restart_starts_a_new_desktop_in_the_same_session(self):
        child, stage, state = self.supervise('restart')
        first = wait(lambda: (s := self.read(state)).get('ready') and s, 30)
        self.assertTrue(first, 'The first desktop never became ready')
        backend = first['backend_pid']
        (stage / 'exit-code').write_text('75')
        second = wait(lambda: (s := self.read(state)).get('desktop_restarts') == 1 and s.get('ready') and s, 30)
        self.assertTrue(second, self.read(state))
        self.assertNotEqual(second['launcher_pid'], first['launcher_pid'])
        self.assertEqual(second['backend_pid'], backend)  # the same Sway: no new session, no Log In
        self.assertNotIn('desktop_exit', second)
        self.assertIsNone(child.poll())
        starts = (stage / 'starts').read_text().splitlines()
        self.assertEqual(len(starts), 2)
        # Each desktop is told this supervisor can restart it.
        self.assertTrue(all(line.split()[1] == '75' for line in starts), starts)
        self.assertIn('desktop: restart 1 requested', (stage / 'evidence/supervisor.txt').read_text())
        # Any other exit still ends the session.
        (stage / 'exit-code').write_text('0')
        child.wait(30)  # (its own exit status also reports the stand-in's leftover runtime files)
        self.assertEqual(self.read(state).get('desktop_exit'), 0)
        self.assertTrue(self.read(state).get('finished'))
        self.assertEqual(len((stage / 'starts').read_text().splitlines()), 2)

    def test_a_failing_desktop_is_not_restarted(self):
        child, stage, state = self.supervise('failure')
        self.assertTrue(wait(lambda: self.read(state).get('ready'), 30))
        (stage / 'exit-code').write_text('1')
        self.assertNotEqual(child.wait(30), 0)
        self.assertEqual(self.read(state).get('desktop_exit'), 1)
        self.assertNotIn('desktop_restarts', self.read(state))


if __name__ == '__main__':
    unittest.main()
