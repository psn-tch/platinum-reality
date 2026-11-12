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
        for tool in ('sway', 'swaymsg'):
            if not shutil.which(tool):
                raise unittest.SkipTest(tool + ' is not installed')
        for binary in ('platinum-lock', 'virtual-keyboard-driver', 'pointer-probe', 'output-capture'):
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
        renderer = os.environ.get('PLATINUM_LOCK_TEST_RENDERER', 'software')
        if renderer not in ('software', 'opengl'):
            raise ValueError('PLATINUM_LOCK_TEST_RENDERER must be software or opengl')
        environment.update(XDG_RUNTIME_DIR=str(cls.runtime), WLR_BACKENDS='headless', WLR_HEADLESS_OUTPUTS='2',
                           WLR_LIBINPUT_NO_DEVICES='1', WLR_RENDERER='pixman' if renderer == 'software' else 'gles2')
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
                               QT_QUICK_BACKEND='software' if renderer == 'software' else 'rhi')
        for key in ('WLR_BACKENDS', 'WLR_HEADLESS_OUTPUTS', 'WLR_LIBINPUT_NO_DEVICES', 'WLR_RENDERER'):
            cls.environment.pop(key)
        # The test window (a fixed-size, so floating and centred, 320 x 240) opens on HEADLESS-1.
        subprocess.run(['swaymsg', 'focus', 'output', 'HEADLESS-1'], env=cls.environment,
                       stdout=subprocess.DEVNULL, check=True, timeout=10)
        import json
        cls.probe_workspace = next(o['current_workspace'] for o in json.loads(subprocess.run(
            ['swaymsg', '-r', '-t', 'get_outputs'], env=cls.environment, capture_output=True, text=True,
            timeout=10, check=True).stdout) if o['name'] == 'HEADLESS-1')
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
        # In logical coordinates, whatever the output's scale and rotation.
        capture = subprocess.run([str(BUILD / 'output-capture'), output], env=self.environment,
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

    def locker(self, extra=(), pass_fds=(), log_path=None):
        reader, writer = os.pipe()
        diagnostics = os.environ.get('PLATINUM_LOCK_TEST_EVIDENCE')
        environment = self.environment.copy()
        log = log_path.open('xb') if log_path else None
        if diagnostics and not log:
            directory = Path(diagnostics)
            directory.mkdir(parents=True, exist_ok=True, mode=0o700)
            log = (directory / (self._testMethodName + '-' + str(writer) + '-' + str(time.monotonic_ns()) + '.txt')).open('xb')
            # Optional private-fixture trace; never set by the physical launcher.
            # All keys here are the source's disposable test passwords.
            if os.environ.get('PLATINUM_LOCK_TEST_TRACE') == '1':
                environment['WAYLAND_DEBUG'] = 'client'
        # An empty disposable profile: no desktop picture, so the plain lock color is measurable.
        process = subprocess.Popen([str(BUILD / 'platinum-lock'), '--ready-fd', str(writer),
                                    '--pam-service', 'platinum-lock-test', '--pam-confdir', str(self.pam),
                                    '--profile', str(self.pam.parent / 'profile'), *extra],
                                   env=environment, pass_fds=(writer, *pass_fds), stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=log or subprocess.DEVNULL)
        if log:
            log.close()
        os.close(writer)
        self.addCleanup(lambda: process.poll() is None and (process.kill(), process.wait(5)))
        return process, reader

    def output(self, command):
        import json
        result = subprocess.run(['swaymsg', '-r', command], env=self.environment,
                                capture_output=True, text=True, timeout=10, check=True)
        self.assertTrue(all(item.get('success') for item in json.loads(result.stdout)), result.stdout)

    def restore_outputs(self):
        """Both screens as set up, with the test window's workspace shown on HEADLESS-1 again:
        disabling a screen moves its workspaces to the other one, and they stay there."""
        self.output('output HEADLESS-1 enable resolution 1024x700 scale 1 transform normal position 0 0')
        self.output('output HEADLESS-2 enable resolution 800x600 scale 1 transform normal position 1024 0')
        self.output('workspace ' + self.probe_workspace)
        self.output('move workspace to output HEADLESS-1')
        self.output('focus output HEADLESS-1')

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
        # The Log In dialog (440 x 236, centred) is on every screen: the keyboard stays on one lock
        # window, which need not be the screen in use, so every screen shows the shared field.
        dialog = lambda output, width, height: self.pixel(output, width // 2 - 210, height // 2 + 100)
        self.assertTrue(wait(lambda: all(dialog(*s) == DIALOG_COLOR for s in screens), 5),
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
        # The ready pipe confirms secure compositor coverage, not a rendered
        # password widget. Observe the replacement's dialog before injecting
        # the first synthetic key burst, just as for the initial locker above.
        self.assertTrue(wait(lambda: any(dialog(*s) == DIALOG_COLOR for s in screens), 5),
                        'The replacement is locked but has no rendered Log In dialog')
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(third.wait(15), 0)
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 512, 350) == PROBE_COLOR, 5),
                        'The desktop did not return after unlocking')

    def test_output_disappearance_and_return_keep_a_usable_lock(self):
        # Sleep/docking can withdraw every wl_output, briefly leaving Qt's placeholder
        # QScreen. Exercise that transition without suspending or touching the real seat.
        def output(command):
            result = subprocess.run(['swaymsg', '-r', command], env=self.environment,
                                    capture_output=True, text=True, timeout=10, check=True)
            import json
            self.assertTrue(all(item.get('success') for item in json.loads(result.stdout)), result.stdout)

        self.addCleanup(self.restore_outputs)
        process, reader = self.locker()
        self.assertTrue(self.ready(reader), 'The lock was not confirmed')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 5, 5) == LOCK_COLOR, 5))
        def dialog_present():
            return any(self.pixel(name, x, y) == DIALOG_COLOR for name, x, y in
                       (('HEADLESS-1', 302, 450), ('HEADLESS-2', 190, 400)))
        self.assertTrue(wait(dialog_present, 5), 'The initial lock has no dialog on either screen')
        output('output * power off')
        time.sleep(0.3)
        output('output * power on')
        self.assertTrue(wait(dialog_present, 5),
                        'The Log In dialog did not return after output power cycling')
        output('output HEADLESS-1 disable')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-2', 5, 5) == LOCK_COLOR, 5))
        output('output HEADLESS-2 disable')
        time.sleep(0.3)  # Allow the client to receive both removed globals and the placeholder.
        self.assertIsNone(process.poll(), 'Losing all displays killed the locker')
        output('output HEADLESS-1 enable resolution 1024x700 scale 1 transform normal position 0 0')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 5, 5) == LOCK_COLOR, 5),
                        'The returning screen has no lock surface')
        # A colored background alone is not a usable lock: assert the dialog and real
        # keyboard authentication return, without releasing ext-session-lock between them.
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 302, 450) == DIALOG_COLOR, 5),
                        'The returning screen has no usable Log In dialog')
        # The laptop panel also returns with its own rotation and fractional scale.
        # output-capture makes these pixel assertions use logical coordinates.
        output('output HEADLESS-1 resolution 1610x2300 transform 270 scale 2.3 position 0 0')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 290, 450) == DIALOG_COLOR, 5),
                        'The rotated 2.3x screen lost its Log In dialog')
        self.type('type wrong horse')
        self.type('key return')
        time.sleep(1.5)
        self.assertIsNone(process.poll(), 'A wrong password released the returning display')
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(process.wait(15), 0)
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 512, 350) == PROBE_COLOR, 5),
                        'The desktop did not return after authenticating on the returned screen')

    def test_replacement_without_outputs_authenticates_on_returning_external(self):
        # The resume replacement can start before a sleeping dock exports any
        # wl_output. It must survive Qt's placeholder and draw on the external
        # screen when that alone returns; process/readiness alone are not enough.
        self.addCleanup(self.restore_outputs)
        first, reader = self.locker()
        self.assertTrue(self.ready(reader))
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-2', 5, 5) == LOCK_COLOR, 5))
        first.kill()
        first.wait(5)
        self.output('output HEADLESS-1 disable')
        self.output('output HEADLESS-2 disable')
        time.sleep(0.3)
        replacement, reader = self.locker()
        self.assertTrue(self.ready(reader), 'The no-output replacement did not acquire the abandoned lock')
        time.sleep(0.4)
        self.assertIsNone(replacement.poll(), 'A replacement exited without authentication while no outputs existed')
        self.output('output HEADLESS-2 enable resolution 900x680 scale 1 transform normal position 0 0')
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-2', 240, 440) == DIALOG_COLOR, 5),
                        'The external display returned without a usable Log In dialog')
        self.type('type wrong horse')
        self.type('key return')
        time.sleep(1.5)
        self.assertIsNone(replacement.poll(), 'A wrong password unlocked the returning external display')
        self.assertEqual(self.pixel('HEADLESS-2', 5, 5), LOCK_COLOR)
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(replacement.wait(15), 0)

    def outputs(self):
        import json
        result = subprocess.run(['swaymsg', '-r', '-t', 'get_outputs'], env=self.environment,
                                capture_output=True, text=True, timeout=10, check=True)
        return {o['name']: o for o in json.loads(result.stdout)}

    def plug(self, width, height, x):
        """A new monitor, as a dock's arrives: a connector the session has not seen before."""
        before = set(self.outputs())
        self.output('create_output')
        name = wait(lambda: next(iter(set(self.outputs()) - before), None), 5)
        self.assertIsNotNone(name, 'create_output made no monitor')
        self.addCleanup(lambda: name in self.outputs() and self.output('output ' + name + ' unplug'))
        self.output(f'output {name} enable resolution {width}x{height} scale 1 transform normal position {x} 0')
        return name

    def dialog_shown(self, output, width, height):
        return self.pixel(output, width // 2 - 210, height // 2 + 100) == DIALOG_COLOR

    def typed_dots(self, output, width, height):
        """How many dark pixels the password field's text row has (its bullets) on this screen."""
        left, top = width // 2 - 220, height // 2 - 118
        return sum(sum(self.pixel(output, left + x, top + 101)) < 200 for x in range(120, 200, 2))

    def test_dock_wake_shows_the_dialog_on_the_monitor_without_the_keyboard(self):
        # A laptop on a USB-C dock (Lily Hewitt, 2026-11-11): at sleep the dock's monitor goes;
        # on wake the replacement locker covers the laptop panel first, so the compositor gives
        # that panel the keyboard for the whole lock (Sway never moves it on a click). The dock's
        # monitor then returns as a new connector. It must show the Log In dialog and the typing,
        # not only the desktop picture with the password going blind into the panel.
        self.addCleanup(self.restore_outputs)
        self.output('output HEADLESS-2 disable')
        dock = self.plug(900, 680, 1024)
        first, reader = self.locker()
        self.assertTrue(self.ready(reader), 'The pre-sleep lock was not confirmed')
        self.assertTrue(wait(lambda: self.pixel(dock, 5, 5) == LOCK_COLOR, 5))
        # Sleep: the dock's monitor disappears. Wake: the session replaces the pre-sleep locker.
        self.output('output ' + dock + ' unplug')
        first.kill()
        first.wait(5)
        replacement, reader = self.locker()
        self.assertTrue(self.ready(reader), 'The after-wake replacement did not take over the lock')
        self.assertTrue(wait(lambda: self.dialog_shown('HEADLESS-1', 1024, 700), 5),
                        'The laptop panel has no Log In dialog after wake')
        # The dock wakes up a moment later.
        dock = self.plug(900, 680, 1024)
        self.assertTrue(wait(lambda: self.pixel(dock, 5, 5) == LOCK_COLOR, 5), 'The returning monitor is not locked')
        self.assertTrue(wait(lambda: self.dialog_shown(dock, 900, 680), 5),
                        'The returning monitor shows only the desktop picture, without a Log In dialog')
        self.assertTrue(self.dialog_shown('HEADLESS-1', 1024, 700), 'The laptop panel lost its Log In dialog')
        # Typing shows on every screen, whichever one the compositor gave the keyboard.
        self.type('type wrong horse')
        self.assertTrue(wait(lambda: self.typed_dots(dock, 900, 680) > 5, 5),
                        'Typing did not show in the returning monitor\'s dialog')
        self.type('key return')
        time.sleep(1.5)
        self.assertIsNone(replacement.poll(), 'A wrong password unlocked the docked laptop')
        self.assertTrue(wait(lambda: self.typed_dots(dock, 900, 680) == 0, 5),
                        'A rejected password stayed in the returning monitor\'s dialog')
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(replacement.wait(15), 0)

    def test_focused_panel_disappears_but_external_still_accepts_authentication(self):
        # Unlike losing every output, a docked laptop may remove only the
        # focused panel. The surviving external must show a dialog and accept
        # actual keys, rather than leaving a wallpaper-only locked surface.
        self.addCleanup(self.restore_outputs)
        self.output('focus output HEADLESS-1')
        log = Path(self.directory.name) / (self._testMethodName + '.txt')
        process, reader = self.locker(log_path=log)
        self.assertTrue(self.ready(reader))
        self.assertTrue(wait(lambda: self.pixel('HEADLESS-1', 5, 5) == LOCK_COLOR, 5))
        # The compositor chooses the initial lock-surface keyboard focus, and the lock logs
        # where it went. Either output represents the focused panel that is about to disappear.
        screens = (('HEADLESS-1', 302, 450), ('HEADLESS-2', 190, 400))
        def focused_output():
            lines = [line for line in log.read_text().splitlines() if 'Lock screen: keyboard ' in line]
            name = lines[-1].rsplit(' ', 1)[-1] if lines and ' keyboard on ' in lines[-1] else None
            return name if name in dict((s[0], s) for s in screens) else None
        focused = wait(focused_output, 5)
        self.assertIsNotNone(focused, 'The initial lock never logged a keyboard screen')
        survivor, x, y = next(s for s in screens if s[0] != focused)
        self.output('output ' + focused + ' disable')
        self.assertTrue(wait(lambda: self.pixel(survivor, x, y) == DIALOG_COLOR, 5),
                        'Removing the focused panel left only the wallpaper on the external')
        self.type('type wrong horse')
        self.type('key return')
        time.sleep(1.5)
        self.assertIsNone(process.poll(), 'A wrong password unlocked the external display')
        self.type('type correct horse')
        self.type('key return')
        self.assertEqual(process.wait(15), 0)


if __name__ == '__main__':
    unittest.main()
