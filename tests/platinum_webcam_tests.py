#!/usr/bin/env python3
"""The Camera switch's helper, on a throwaway root with a fake sysfs: a laptop's built-in webcam
(video interfaces plus its microphone), a plugged-in camera, a keyboard, a MIPI camera and a
virtual camera."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('platinum_webcam', HERE.parent / 'scripts/platinum_webcam.py')
webcam = importlib.util.module_from_spec(spec)
spec.loader.exec_module(webcam)


EYETOY = 'usb:v054Cp0155d0100dc00dsc00dp00icFFiscFFipFFin00'
WIFI = 'usb:v0BDAp8812d0200dc00dsc00dp00icFFiscFFipFFin00'
# What modprobe -R answers: the modules whose USB IDs match.
ALIASES = {EYETOY: 'gspca_ov519\n', WIFI: 'rtw88_8821cu\n'}


class Runner:
    """Stands in for subprocess.run; records commands, fails the ones listed."""

    def __init__(self, failing=(), busy=0):
        self.commands = []
        self.failing = failing
        self.busy = busy  # How many unloads fail first, as while a program still holds the camera.

    def __call__(self, command, **_):
        if command[:2] == ['modprobe', '-R']:
            found = ALIASES.get(command[2])
            return subprocess.CompletedProcess(command, 0 if found else 1, found or '', '')
        self.commands.append(command)
        if command[:2] == ['modprobe', '-r'] and self.busy:
            self.busy -= 1
            return subprocess.CompletedProcess(command, 1)
        return subprocess.CompletedProcess(command, 1 if command in self.failing else 0)


class CameraSwitchTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        usb = self.root / webcam.USB
        usb.mkdir(parents=True)
        self.device(usb, '1-6', product='Integrated Camera', manufacturer='Chicony',
                    interfaces={'1-6:1.0': '0e', '1-6:1.1': '0e', '1-6:1.2': '01', '1-6:1.3': '01'})
        self.device(usb, '3-2', manufacturer='Logitech', interfaces={'3-2:1.0': '0e', '3-2:1.1': '0e'})
        self.device(usb, '1-10', product='USB Keyboard', interfaces={'1-10:1.0': '03'})
        (usb.parent / 'drivers_probe').write_text('')
        (self.root / 'sys/module/uvcvideo').mkdir(parents=True)
        self.sleeps = []
        v4l = self.root / webcam.VIDEO4LINUX
        v4l.mkdir(parents=True)
        self.node(v4l, 'video0', 'Integrated Camera: Integrated C', usb / '1-6:1.0')
        self.node(v4l, 'video1', 'Integrated Camera: Integrated C', usb / '1-6:1.0')
        self.node(v4l, 'video2', 'Logi C270', usb / '3-2:1.0')
        mipi = self.root / 'sys/devices/pci0000:00/0000:00:05.0'
        mipi.mkdir(parents=True)
        self.node(v4l, 'video3', 'Intel IPU6 ISYS Capture 0', mipi)
        virtual = self.root / 'sys/devices/virtual/video4linux/video9'
        virtual.mkdir(parents=True)
        (virtual / 'name').write_text('OBS Virtual Camera\n')
        (v4l / 'video9').symlink_to(virtual)

    def tearDown(self):
        self.folder.cleanup()

    def device(self, usb, name, interfaces, product='', manufacturer=''):
        folder = self.root / 'sys/devices/usb' / name
        folder.mkdir(parents=True)
        if product:
            (folder / 'product').write_text(product + '\n')
        if manufacturer:
            (folder / 'manufacturer').write_text(manufacturer + '\n')
        (folder / 'authorized').write_text('1\n')
        (usb / name).symlink_to(folder)
        for interface, kind in interfaces.items():
            path = folder / interface
            path.mkdir()
            (path / 'bInterfaceClass').write_text(kind + '\n')
            (path / 'authorized').write_text('1\n')
            (usb / interface).symlink_to(path)

    def node(self, v4l, name, title, device):
        folder = self.root / 'sys/devices/nodes' / name
        folder.mkdir(parents=True)
        (folder / 'name').write_text(title + '\n')
        (folder / 'device').symlink_to(device)
        (v4l / name).symlink_to(folder)

    def authorized(self, interface):
        return (self.root / webcam.USB / interface / 'authorized').read_text().strip()

    def test_status_lists_usb_cameras_and_the_ones_it_cannot_switch(self):
        state = webcam.status(self.root)
        self.assertFalse(state['off'])
        self.assertEqual(state['cameras'], [{'id': '1-6', 'name': 'Integrated Camera', 'blocked': False},
                                            {'id': '3-2', 'name': 'Logitech Camera', 'blocked': False}])
        # The MIPI camera can't be switched; the virtual one isn't a camera to switch.
        self.assertEqual(state['others'], ['Intel IPU6 ISYS Capture 0'])

    def test_off_blocks_every_video_interface_and_nothing_else(self):
        run = Runner()
        webcam.turn_off(self.root, run)
        for interface in ('1-6:1.0', '1-6:1.1', '3-2:1.0', '3-2:1.1'):
            self.assertEqual(self.authorized(interface), '0', interface)
        # The webcam's microphone and the keyboard are untouched.
        for interface in ('1-6:1.2', '1-6:1.3', '1-10:1.0'):
            self.assertEqual(self.authorized(interface), '1', interface)
        self.assertEqual((self.root / 'sys/devices/usb/1-6/authorized').read_text().strip(), '1')
        self.assertEqual((self.root / webcam.MODPROBE).read_text(), webcam.MODPROBE_TEXT)
        self.assertEqual((self.root / webcam.UDEV).read_text(), webcam.UDEV_TEXT)
        self.assertEqual(oct((self.root / webcam.UDEV).stat().st_mode & 0o777), '0o644')
        self.assertIn(['udevadm', 'control', '--reload'], run.commands)
        self.assertEqual(run.commands[-1], ['modprobe', '-r', 'uvcvideo'])
        self.assertEqual(run.commands.count(['modprobe', '-r', 'uvcvideo']), 1)
        state = webcam.status(self.root)
        self.assertTrue(state['off'])
        self.assertTrue(all(c['blocked'] for c in state['cameras']))

    def test_rules_say_what_the_kernel_needs(self):
        # Every video interface, as it appears; uvcvideo can never be loaded while off.
        self.assertIn('ENV{DEVTYPE}=="usb_interface"', webcam.UDEV_TEXT)
        self.assertIn('ATTR{bInterfaceClass}=="0e"', webcam.UDEV_TEXT)
        self.assertIn('ATTR{authorized}="0"', webcam.UDEV_TEXT)
        self.assertIn('install uvcvideo /bin/false', webcam.MODPROBE_TEXT.splitlines())

    def test_off_waits_for_a_program_to_let_go_of_the_driver(self):
        run = Runner(busy=3)
        webcam.turn_off(self.root, run, self.sleeps.append)
        self.assertEqual(run.commands.count(['modprobe', '-r', 'uvcvideo']), 4)
        self.assertEqual(len(self.sleeps), 3)

    def test_off_gives_up_on_unloading_but_the_camera_stays_blocked(self):
        run = Runner(busy=99)
        webcam.turn_off(self.root, run, self.sleeps.append)
        self.assertEqual(run.commands.count(['modprobe', '-r', 'uvcvideo']), webcam.UNLOAD_TRIES)
        self.assertTrue(all(c['blocked'] for c in webcam.status(self.root)['cameras']))

    def test_off_does_not_unload_a_driver_that_is_not_loaded(self):
        (self.root / 'sys/module/uvcvideo').rmdir()
        run = Runner()
        webcam.turn_off(self.root, run)
        self.assertNotIn(['modprobe', '-r', 'uvcvideo'], run.commands)

    def test_on_authorizes_every_interface_before_probing_any(self):
        webcam.turn_off(self.root, Runner())
        probe = self.root / 'sys/bus/usb/drivers_probe'
        probe.unlink()
        seen = []

        class Probe:
            # Records which interfaces were authorized when each probe happened.
            def __init__(self, test):
                self.test = test

            def write(self, name):
                seen.append((name, [i for i in ('1-6:1.0', '1-6:1.1', '3-2:1.0', '3-2:1.1')
                                    if self.test.authorized(i) == '1']))

            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False

        probe.write_text('')
        real_open = open
        webcam.open = lambda path, mode='r': Probe(self) if Path(path) == probe else real_open(path, mode)
        try:
            webcam.turn_on(self.root, Runner())
        finally:
            del webcam.open
        self.assertEqual([name for name, _ in seen], ['1-6:1.0', '1-6:1.1', '3-2:1.0', '3-2:1.1'])
        for _, authorized in seen:
            self.assertEqual(len(authorized), 4)

    def test_on_gives_everything_back(self):
        webcam.turn_off(self.root, Runner())
        run = Runner()
        webcam.turn_on(self.root, run)
        self.assertFalse((self.root / webcam.MODPROBE).exists())
        self.assertFalse((self.root / webcam.UDEV).exists())
        for interface in ('1-6:1.0', '1-6:1.1', '3-2:1.0', '3-2:1.1'):
            self.assertEqual(self.authorized(interface), '1', interface)
        # The driver is loaded before the interfaces are let back in, then each is probed.
        self.assertEqual(run.commands, [['udevadm', 'control', '--reload'], ['modprobe', 'uvcvideo']])
        self.assertEqual((self.root / 'sys/bus/usb/drivers_probe').read_text(), '3-2:1.1')
        self.assertFalse(webcam.status(self.root)['off'])

    def test_off_twice_and_on_twice_are_harmless(self):
        webcam.turn_off(self.root, Runner())
        webcam.turn_off(self.root, Runner())
        self.assertTrue(webcam.is_off(self.root))
        webcam.turn_on(self.root, Runner())
        webcam.turn_on(self.root, Runner())
        self.assertFalse(webcam.is_off(self.root))

    def test_a_camera_plugged_in_while_off_counts_as_on_until_the_rule_blocks_it(self):
        webcam.turn_off(self.root, Runner())
        (self.root / webcam.USB / '3-2:1.0' / 'authorized').write_text('1\n')
        cameras = {c['id']: c['blocked'] for c in webcam.status(self.root)['cameras']}
        self.assertEqual(cameras, {'1-6': True, '3-2': False})

    def test_someone_elses_files_are_never_replaced_or_removed(self):
        mine = self.root / webcam.MODPROBE
        mine.parent.mkdir(parents=True)
        mine.write_text('options uvcvideo quirks=128\n')
        with self.assertRaises(webcam.CameraError):
            webcam.turn_off(self.root, Runner())
        self.assertEqual(mine.read_text(), 'options uvcvideo quirks=128\n')
        self.assertFalse((self.root / webcam.UDEV).exists())
        self.assertEqual(self.authorized('1-6:1.0'), '1')
        self.assertFalse(webcam.is_off(self.root))
        with self.assertRaises(webcam.CameraError):
            webcam.turn_on(self.root, Runner())
        self.assertEqual(mine.read_text(), 'options uvcvideo quirks=128\n')

    def test_on_says_so_when_the_driver_will_not_load(self):
        webcam.turn_off(self.root, Runner())
        with self.assertRaisesRegex(webcam.CameraError, 'uvcvideo'):
            webcam.turn_on(self.root, Runner(failing=[['modprobe', 'uvcvideo']]))
        # The files are already gone, so the next restart brings the camera back anyway.
        self.assertFalse(webcam.is_off(self.root))

    def test_off_says_so_when_a_camera_stays_on(self):
        blocked = self.root / webcam.USB / '3-2:1.1' / 'authorized'
        blocked.unlink()
        blocked.mkdir()  # Writing to it fails, as a vanished or stuck interface would.
        with self.assertRaisesRegex(webcam.CameraError, '3-2:1.1|Logitech'):
            webcam.turn_off(self.root, Runner())
        self.assertTrue(webcam.is_off(self.root))  # The rest stays off and the next boot blocks it.

    def eyetoy(self, wifi=True):
        """Lily Hewitt's PS2 EyeToy (gspca_ov519, a vendor-specific interface, and its microphone)."""
        usb = self.root / webcam.USB
        self.device(usb, '1-3', product='EyeToy USB camera Namtai', manufacturer='Sony',
                    interfaces={'1-3:1.0': 'ff', '1-3:1.1': '01'})
        (usb / '1-3:1.0' / 'modalias').write_text(EYETOY + '\n')
        (usb / '1-3:1.0' / 'video4linux' / 'video4').mkdir(parents=True)
        if wifi:
            self.device(usb, '1-4', product='802.11ac WLAN Adapter', interfaces={'1-4:1.0': 'ff'})
            (usb / '1-4:1.0' / 'modalias').write_text(WIFI + '\n')
        for module in ('gspca_ov519', 'gspca_main'):
            (self.root / 'sys/module' / module).mkdir(parents=True)

    def test_an_eyetoy_is_a_camera_the_switch_covers(self):
        self.eyetoy()
        names = {c['id']: c['name'] for c in webcam.status(self.root, Runner())['cameras']}
        self.assertEqual(names['1-3'], 'EyeToy USB camera Namtai')
        self.assertNotIn('1-4', names)  # A Wi-Fi adapter's vendor-specific interface is no camera.
        run = Runner()
        webcam.turn_off(self.root, run, lambda _: None)
        self.assertEqual(self.authorized('1-3:1.0'), '0')
        self.assertEqual(self.authorized('1-3:1.1'), '1')  # Its microphone stays.
        self.assertEqual(self.authorized('1-4:1.0'), '1')
        self.assertIn('install gspca_main /bin/false', webcam.MODPROBE_TEXT.splitlines())
        # gspca's own drivers before gspca_main, then uvcvideo, in one modprobe.
        self.assertEqual(run.commands[-1], ['modprobe', '-r', 'gspca_ov519', 'gspca_main', 'uvcvideo'])
        # Blocked, its video device is gone, but it is still known by its USB ID (as after a restart).
        (self.root / webcam.USB / '1-3:1.0' / 'video4linux' / 'video4').rmdir()
        (self.root / webcam.USB / '1-3:1.0' / 'video4linux').rmdir()
        eyetoy = [c for c in webcam.status(self.root, Runner())['cameras'] if c['id'] == '1-3'][0]
        self.assertTrue(eyetoy['blocked'])
        run = Runner()
        webcam.turn_on(self.root, run)
        self.assertEqual(self.authorized('1-3:1.0'), '1')
        self.assertIn(['modprobe', '-b', EYETOY], run.commands)  # Its own driver, gspca_ov519.
        self.assertNotIn(['modprobe', '-b', WIFI], run.commands)

    def test_the_udev_rule_also_catches_any_usb_video_device(self):
        lines = webcam.UDEV_TEXT.splitlines()
        rule = [line for line in lines if 'SUBSYSTEM=="video4linux"' in line][0]
        self.assertIn('SUBSYSTEMS=="usb"', rule)  # Never a virtual camera (v4l2loopback) or a PCI capture card.
        self.assertIn("echo 0 > /sys%p/device/authorized", rule)

    def test_no_cameras_at_all(self):
        empty = Path(self.folder.name) / 'empty'
        empty.mkdir()
        self.assertEqual(webcam.status(empty), {'off': False, 'cameras': [], 'others': []})
        run = Runner()
        webcam.turn_off(empty, run)
        self.assertTrue(webcam.is_off(empty))
        webcam.turn_on(empty, run)
        self.assertNotIn(['modprobe', 'uvcvideo'], run.commands)  # Nothing to load it for.


class HelperTests(unittest.TestCase):
    """The pkexec entry point: arguments, and refusing to change anything unless root."""

    def helper(self, *arguments):
        process = subprocess.run([sys.executable, '-B', str(HERE.parent / 'scripts/platinum-webcam'), *arguments],
                                 capture_output=True, text=True,
                                 env={**os.environ, 'PYTHONPATH': str(HERE.parent / 'scripts')})
        return process.returncode, [json.loads(line) for line in process.stdout.splitlines()]

    def test_status_runs_as_anyone(self):
        code, lines = self.helper('status')
        self.assertEqual(code, 0)
        self.assertEqual(set(lines[0]), {'off', 'cameras', 'others'})

    def test_unknown_arguments_are_refused(self):
        code, lines = self.helper('off', 'now')
        self.assertEqual(code, 2)
        self.assertIn('Usage', lines[0]['error'])

    @unittest.skipIf(os.geteuid() == 0, 'runs as root')
    def test_switching_needs_root(self):
        for argument in ('on', 'off'):
            code, lines = self.helper(argument)
            self.assertEqual(code, 1)
            self.assertIn('administrator', lines[0]['error'])


if __name__ == '__main__':
    unittest.main()
