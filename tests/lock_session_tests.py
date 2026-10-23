"""platinum-lock on a real compositor: an owned headless Sway (two outputs, private runtime
directory, no input devices) that this test starts itself. Never touches a user's session.

Proves from screen captures that the lock covers every output, that a wrong password, a second
locker or a killed locker never reveals the desktop, and that only the right password does.
Passwords go through a disposable PAM directory, typed by a virtual keyboard on the test seat.
"""
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BUILD = Path(os.environ.get('FRANKENSTEIN_BUILD_DIR', Path(__file__).resolve().parents[1] / 'build'))
LOCK_COLOR = (0x66, 0x66, 0x99)
DIALOG_COLOR = (0xdd, 0xdd, 0xdd)
PROBE_COLOR = (0x30, 0x60, 0xa0)


def wait(condition, timeout, step=0.05):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = condition()
        if value:
            return value
        time.sleep(step)
    return condition()


class LockSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for tool in ('sway', 'grim', 'swaymsg'):
            if not shutil.which(tool):
                raise unittest.SkipTest(tool + ' is not installed')
        for binary in ('platinum-lock', 'virtual-keyboard-driver', 'pointer-probe'):
            if not (BUILD / binary).is_file():
                raise unittest.SkipTest(binary + ' is not built')
        cls.directory = tempfile.TemporaryDirectory()
        root = Path(cls.directory.name)
        cls.runtime = root / 'runtime'
        cls.runtime.mkdir(mode=0o700)
        (root / 'sway.conf').write_text('xwayland disable\nswaybg_command -\nswaynag_command -\n'
                                        'default_border none\nfocus_follows_mouse no\n'
                                        'output HEADLESS-1 resolution 1024x700 position 0 0\n'
                                        'output HEADLESS-2 resolution 800x600 position 1024 0\n')
        # Disposable PAM: the password is "correct horse" (see tests/lock_tests.cpp).
        cls.pam = root / 'pam'
        cls.pam.mkdir()
        check = cls.pam / 'check-password'
        check.write_text("#!/bin/bash\nIFS= read -r -d '' password\n[ \"$password\" = 'correct horse' ]\n")
        check.chmod(0o700)
        (cls.pam / 'platinum-lock-test').write_text(
            'auth required pam_exec.so expose_authtok quiet %s\naccount required pam_permit.so\n' % check)
        environment = {k: v for k, v in os.environ.items()
                       if k not in ('WAYLAND_DISPLAY', 'DISPLAY', 'SWAYSOCK', 'I3SOCK', 'XDG_SESSION_ID',
                                    'WAYLAND_SOCKET', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_STYLE_OVERRIDE')}
        environment.update(XDG_RUNTIME_DIR=str(cls.runtime), WLR_BACKENDS='headless', WLR_HEADLESS_OUTPUTS='2',
                           WLR_LIBINPUT_NO_DEVICES='1', WLR_RENDERER='pixman')
        cls.log = open(root / 'sway.txt', 'wb')
        cls.sway = subprocess.Popen(['sway', '--config', str(root / 'sway.conf')], env=environment,
                                    stdin=subprocess.DEVNULL, stdout=cls.log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
        sockets = wait(lambda: [p for p in cls.runtime.glob('wayland-*') if p.is_socket()]
                       and [p for p in cls.runtime.glob('sway-ipc.*.sock')], 15)
        if not sockets or cls.sway.poll() is not None:
            cls.tearDownClass()
            raise RuntimeError('Owned headless Sway did not start; see ' + str(root / 'sway.txt'))
        display = [p for p in cls.runtime.glob('wayland-*') if p.is_socket()][0]
        cls.environment = dict(environment, WAYLAND_DISPLAY=str(display),
                               SWAYSOCK=str(next(cls.runtime.glob('sway-ipc.*.sock'))),
                               QT_QUICK_BACKEND='software')
        for key in ('WLR_BACKENDS', 'WLR_HEADLESS_OUTPUTS', 'WLR_LIBINPUT_NO_DEVICES', 'WLR_RENDERER'):
            cls.environment.pop(key)
        # The test window (a fixed-size, so floating and centred, 320 x 240) opens on HEADLESS-1.
        subprocess.run(['swaymsg', 'focus', 'output', 'HEADLESS-1'], env=cls.environment,
                       stdout=subprocess.DEVNULL, check=True, timeout=10)
        cls.probe = subprocess.Popen([str(BUILD / 'pointer-probe')], env=cls.environment,
                                     stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        cls.keyboard = subprocess.Popen([str(BUILD / 'virtual-keyboard-driver')], env=cls.environment,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        if cls.keyboard.stdout.readline().strip() != 'driver-ready':
            cls.tearDownClass()
            raise RuntimeError('The virtual keyboard did not start')

    @classmethod
    def tearDownClass(cls):
        for name in ('keyboard', 'probe'):
            process = getattr(cls, name, None)
            if process and process.poll() is None:
                process.kill()
                process.wait(5)
        if getattr(cls, 'sway', None) and cls.sway.poll() is None:
            os.killpg(cls.sway.pid, signal.SIGTERM)
            cls.sway.wait(10)
        if getattr(cls, 'log', None):
            cls.log.close()
        cls.directory.cleanup()

    def type(self, command):
        self.keyboard.stdin.write(command + '\n')
        self.keyboard.stdin.flush()
        self.assertEqual(self.keyboard.stdout.readline().strip(), 'ok')

    def pixel(self, output, x, y):
        capture = subprocess.run(['grim', '-o', output, '-t', 'ppm', '-'], env=self.environment,
                                 capture_output=True, timeout=10, check=True).stdout
        fields, offset = [], 0
        while len(fields) < 4:  # P6 width height maxval, separated by whitespace
            while capture[offset:offset + 1].isspace():
                offset += 1
            end = offset
            while not capture[end:end + 1].isspace():
                end += 1
            fields.append(capture[offset:end])
            offset = end
        width = int(fields[1])
        start = offset + 1 + (y * width + x) * 3
        return tuple(capture[start:start + 3])

    def locker(self, extra=(), pass_fds=()):
        reader, writer = os.pipe()
        # An empty disposable profile: no desktop picture, so the plain lock color is measurable.
        process = subprocess.Popen([str(BUILD / 'platinum-lock'), '--ready-fd', str(writer),
                                    '--pam-service', 'platinum-lock-test', '--pam-confdir', str(self.pam),
                                    '--profile', str(self.pam.parent / 'profile'), *extra],
                                   env=self.environment, pass_fds=(writer, *pass_fds), stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(writer)
        self.addCleanup(lambda: process.poll() is None and (process.kill(), process.wait(5)))
        return process, reader

    def ready(self, reader, timeout=15):
        try:
            readable, _, _ = select.select([reader], [], [], timeout)
            return bool(readable) and os.read(reader, 1) == b'\n'
        finally:
            os.close(reader)

    def test_log_in_reports_the_chosen_session_only_after_authentication(self):
        # Log In before a session starts (the tty1 replacement): the dialog is taller, carries the
        # session choice, and the choice is written only once the password is right.
        chosen, writer = os.pipe()
        os.set_blocking(chosen, False)
        self.addCleanup(os.close, chosen)
        process, reader = self.locker(('--login', '--default-session', 'kde', '--session-fd', str(writer)),
                                      pass_fds=(writer,))
        os.close(writer)
        self.assertTrue(self.ready(reader), 'The Log In was not confirmed')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 5, 5) == LOCK_COLOR, 5))
        self.type('type wrong horse')
        self.type('key return')
        time.sleep(1.5)
        self.assertIsNone(process.poll())
        with self.assertRaises(BlockingIOError):
            os.read(chosen, 16)
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(process.wait(15), 0)
        self.assertEqual(os.read(chosen, 16), b'kde\n')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 512, 350) == PROBE_COLOR, 5))
        # Without --login there is no session to report.
        _, writer = os.pipe()
        refused = subprocess.run([str(BUILD / 'platinum-lock'), '--session-fd', str(writer)], env=self.environment,
                                 pass_fds=(writer,), capture_output=True, timeout=15)
        os.close(writer)
        self.assertNotEqual(refused.returncode, 0)

    def test_only_the_right_password_reveals_the_desktop(self):
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 512, 350) == PROBE_COLOR, 10),
                        'The test window never appeared')
        first, reader = self.locker()
        self.assertTrue(self.ready(reader), 'The lock was not confirmed')
        screens = (('HEADLESS-1', 1024, 700), ('HEADLESS-2', 800, 600))
        for output, _, _ in screens:
            self.assertTrue(wait(lambda: self.pixel(output, 5, 5) == LOCK_COLOR, 5), output)
        # The Log In dialog (440 x 236, centred) is on the screen with the keyboard, and only there,
        # so typing never goes to a field on a screen nobody is looking at.
        dialog = lambda output, width, height: self.pixel(output, width // 2 - 210, height // 2 + 100)
        self.assertTrue(wait(lambda: sorted(dialog(*s) for s in screens) == sorted([DIALOG_COLOR, LOCK_COLOR]), 5),
                        [dialog(*s) for s in screens])
        self.assertNotEqual(self.pixel('HEADLESS-1', 512, 350), PROBE_COLOR)

        # A second locker is refused while the first holds the screen.
        second, reader = self.locker()
        self.assertEqual(second.wait(15), 5)
        os.close(reader)
        self.assertIsNone(first.poll())

        # A wrong password keeps everything covered.
        self.type('type wrong horse')
        self.type('key return')
        time.sleep(1.5)
        self.assertIsNone(first.poll())
        self.assertEqual(self.pixel('HEADLESS-1', 5, 5), LOCK_COLOR)

        # A killed locker is not an unlock: the compositor keeps the outputs locked.
        first.kill()
        first.wait(5)
        time.sleep(1)
        for output in ('HEADLESS-1', 'HEADLESS-2'):
            self.assertNotEqual(self.pixel(output, 512 if output == 'HEADLESS-1' else 400, 350), PROBE_COLOR)

        # A new locker takes over the abandoned lock, and the right password ends it.
        third, reader = self.locker()
        self.assertTrue(self.ready(reader), 'A replacement locker could not take over')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 5, 5) == LOCK_COLOR, 5))
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(third.wait(15), 0)
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 512, 350) == PROBE_COLOR, 5),
                        'The desktop did not return after unlocking')


if __name__ == '__main__':
    unittest.main()
