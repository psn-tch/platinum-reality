"""The Control Strip's Camera switch (docs/camera-switch.md; Lily Hewitt, 2026-11-12: "like nothing can
access it if its toggled off").

Shared by the root helper (platinum-webcam) and its tests. Off is enforced by the kernel, not by
asking programs nicely:
- every USB camera interface is deauthorized, so its driver is unbound at once and cannot bind again;
  a program using the camera loses it then and there. A camera interface is one of the USB Video
  Class (bInterfaceClass 0e: nearly every webcam), one with a video device, or one whose USB ID is
  a gspca driver's (older webcams such as Lily Hewitt's PS2 EyeToy, gspca_ov519). The webcam's
  microphone is a separate (audio) interface and keeps working;
- a udev rule deauthorizes them as they appear (a camera plugged in later, and every boot);
- the uvcvideo and gspca drivers are unloaded and modprobe refuses to load them, so nothing can bind
  at boot before the udev rule runs.
On removes the rule and the modprobe file, loads each camera's driver again and authorizes the
interfaces.
Cameras that are not USB video interfaces (some laptops' MIPI cameras) cannot be switched this way;
status lists them so the menu can say so.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import time

MARK = '# platinum-webcam'
MODPROBE = 'etc/modprobe.d/platinum-webcam-off.conf'
UDEV = 'etc/udev/rules.d/70-platinum-webcam-off.rules'
# gspca's camera drivers all need gspca_main: refusing it refuses every one of them.
MODPROBE_TEXT = (MARK + ': the Control Strip\'s Camera switch is Off. Turning it On removes this file.\n'
                 'install uvcvideo /bin/false\n'
                 'install gspca_main /bin/false\n')
UDEV_TEXT = (MARK + ': the Control Strip\'s Camera switch is Off. Turning it On removes this file.\n'
             'ACTION=="add", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_interface", ATTR{bInterfaceClass}=="0e", '
             'ATTR{authorized}="0"\n'
             '# Any other camera that gets a video device anyway (its driver was still loaded): its USB interface.\n'
             'ACTION=="add", SUBSYSTEM=="video4linux", SUBSYSTEMS=="usb", '
             'RUN+="/bin/sh -c \'echo 0 > /sys%p/device/authorized\'"\n')
USB = 'sys/bus/usb/devices'
VIDEO4LINUX = 'sys/class/video4linux'
VIDEO_CLASS = '0e'
DRIVER = 'uvcvideo'
GSPCA = 'gspca_'
ENVIRONMENT = {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'LC_ALL': 'C'}
# A program that had the camera open keeps the driver loaded until it closes the device it lost.
UNLOAD_TRIES = 10
UNLOAD_WAIT = 0.3


class CameraError(Exception):
    """A change that failed, with the reason shown to the user."""


def _text(path):
    try:
        return path.read_text().strip()
    except OSError:
        return ''


def _ours(path):
    return path.is_file() and not path.is_symlink() and _text(path).startswith(MARK)


def is_off(root=Path('/')):
    """The switch is Off: our modprobe file or udev rule is in place."""
    root = Path(root)
    return _ours(root / MODPROBE) or _ours(root / UDEV)


def _drivers_for(interface, run):
    """The kernel modules whose USB IDs match the interface (modprobe -R; nothing is loaded)."""
    alias = _text(interface / 'modalias')
    if not alias:
        return []
    try:
        process = run(['modprobe', '-R', alias], capture_output=True, text=True, env=ENVIRONMENT)
    except OSError:
        return []
    return process.stdout.split() if process.returncode == 0 else []


def _gspca(interface, run):
    return _text(interface / 'bInterfaceClass').lower() != VIDEO_CLASS and \
        any(name.startswith(GSPCA) for name in _drivers_for(interface, run))


def video_interfaces(root=Path('/'), run=subprocess.run):
    """USB camera interfaces, as sysfs folders (`1-6:1.0`): the video class, a video device, or a
    gspca driver's USB ID (vendor-specific interfaces only are asked about; nothing is loaded)."""
    folder = Path(root) / USB
    if not folder.is_dir():
        return []
    found = []
    for p in sorted(folder.iterdir()):
        if ':' not in p.name:
            continue
        kind = _text(p / 'bInterfaceClass').lower()
        if kind == VIDEO_CLASS or (p / 'video4linux').is_dir() or (kind == 'ff' and _gspca(p, run)):
            found.append(p)
    return found


def _device_name(device):
    product = _text(device / 'product')
    maker = _text(device / 'manufacturer')
    if product:
        return product
    return f'{maker} Camera' if maker else 'USB Camera'


def cameras(root=Path('/'), run=subprocess.run):
    """[{id, name, blocked}] for each USB device with camera interfaces; blocked = none authorized."""
    found = {}
    for interface in video_interfaces(root, run):
        device = interface.parent / interface.name.split(':', 1)[0]
        entry = found.setdefault(device.name, {'id': device.name, 'name': _device_name(device), 'blocked': True})
        if _text(interface / 'authorized') != '0':
            entry['blocked'] = False
    return list(found.values())


def others(root=Path('/')):
    """Names of video capture devices that are not on a USB video interface (and not virtual)."""
    folder = Path(root) / VIDEO4LINUX
    names = []
    if not folder.is_dir():
        return names
    for node in sorted(folder.iterdir()):
        target = (node / 'device').resolve() if (node / 'device').exists() else None
        if target is None or '/virtual/' in str(node.resolve()):
            continue
        if ':' in target.name and (target / 'authorized').exists():
            continue  # A USB camera interface: switched with the rest.
        name = _text(node / 'name') or node.name
        if name not in names:
            names.append(name)
    return names


def status(root=Path('/'), run=subprocess.run):
    """{off, cameras, others}"""
    return {'off': is_off(root), 'cameras': cameras(root, run), 'others': others(root)}


def _write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', dir=path.parent)
    try:
        with os.fdopen(handle, 'w') as file:
            file.write(text)
            file.flush()
            os.fsync(file.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    except BaseException:
        os.unlink(temporary)
        raise


def _remove(path):
    if path.is_symlink() or not path.exists():
        return
    if not _ours(path):
        raise CameraError(f'/{path.name} was not written by the Camera switch; nothing was changed.')
    path.unlink()


def _authorize(interface, value):
    with open(interface / 'authorized', 'w') as file:
        file.write(value)


def _run(command, run):
    return run(command, capture_output=True, text=True, env=ENVIRONMENT).returncode == 0


def _loaded_drivers(root):
    """Camera drivers loaded now: gspca's own before gspca_main, then uvcvideo."""
    modules = Path(root) / 'sys/module'
    names = sorted(p.name for p in modules.iterdir() if p.name.startswith(GSPCA)) if modules.is_dir() else []
    names.sort(key=lambda name: name == 'gspca_main')
    return names + ([DRIVER] if (modules / DRIVER).exists() else [])


def _unload(root, run, sleep):
    """Unload the drivers, waiting a moment for programs that just lost the camera to let go."""
    for attempt in range(UNLOAD_TRIES):
        loaded = _loaded_drivers(root)
        if not loaded or _run(['modprobe', '-r', *loaded], run):
            return
        if attempt + 1 < UNLOAD_TRIES:
            sleep(UNLOAD_WAIT)


def turn_off(root=Path('/'), run=subprocess.run, sleep=time.sleep):
    """Block every USB camera now, later, and after restarting."""
    root = Path(root)
    for relative in (MODPROBE, UDEV):
        path = root / relative
        if path.exists() and not _ours(path):
            raise CameraError(f'/{relative} is not the Camera switch\'s own file; nothing was changed.')
    _write(root / MODPROBE, MODPROBE_TEXT)
    _write(root / UDEV, UDEV_TEXT)
    _run(['udevadm', 'control', '--reload'], run)
    failed = []
    for interface in video_interfaces(root, run):
        try:
            if _text(interface / 'authorized') != '0':
                _authorize(interface, '0')
        except OSError:
            failed.append(interface.name)
    # Nothing is bound to it any more; once unloaded, the modprobe file keeps it out. If a program
    # never lets go, the interfaces stay blocked anyway and the driver is gone after a restart.
    _unload(root, run, sleep)
    still = [c['name'] for c in cameras(root, run) if not c['blocked']]
    if failed or still:
        raise CameraError('Could not turn off ' + ', '.join(still or failed) + '.')


def turn_on(root=Path('/'), run=subprocess.run):
    """Undo turn_off: the files go, the driver loads, and the cameras are let back in."""
    root = Path(root)
    _remove(root / MODPROBE)
    _remove(root / UDEV)
    _run(['udevadm', 'control', '--reload'], run)
    interfaces = video_interfaces(root, run)
    if any(_text(i / 'bInterfaceClass').lower() == VIDEO_CLASS for i in interfaces) and \
            not _run(['modprobe', DRIVER], run):
        raise CameraError('The camera driver (uvcvideo) could not be loaded.')
    # Other cameras: their own driver, found by their USB ID (gspca_ov519 for the EyeToy).
    for interface in interfaces:
        alias = _text(interface / 'modalias')
        if _text(interface / 'bInterfaceClass').lower() != VIDEO_CLASS and alias and \
                not _run(['modprobe', '-b', alias], run):
            raise CameraError('The driver for ' + _device_name(interface.parent / interface.name.split(':', 1)[0]) +
                              ' could not be loaded.')
    probe = root / 'sys/bus/usb/drivers_probe'
    failed = []
    for interface in interfaces:
        try:
            if _text(interface / 'authorized') == '0':
                _authorize(interface, '1')
        except OSError:
            failed.append(interface.name)
    # Then probe: uvcvideo's probe of a camera's control interface claims its streaming interfaces,
    # which must be authorized by then. An interface authorized again is not probed by itself
    # (Documentation/usb/authorization.rst).
    for interface in interfaces:
        try:
            if probe.exists() and interface.name not in failed:
                with open(probe, 'w') as file:
                    file.write(interface.name)
        except OSError:
            pass  # Already bound (claimed with its camera), or no driver: authorizing is what matters.
    if failed:
        raise CameraError('Could not turn on ' + ', '.join(failed) + '.')
