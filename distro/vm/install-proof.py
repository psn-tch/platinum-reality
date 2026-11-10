#!/usr/bin/env python3
"""R2 owned-VM engine proof. No host root, real disks or secret files.

Install to an owned blank qcow2; optionally detach ISO media and prove real
password login. Never infer login acceptance from installation exit status.
"""
import argparse
from datetime import datetime, timezone
import importlib.util
import hashlib
import json
import os
import re
from pathlib import Path
import secrets
import shlex
import subprocess
import sys
import time
import shutil
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from owned import Guest, Lease, run, sha256
from display_profiles import GPUS

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('verify_iso', Path(__file__).with_name('verify-iso.py'))
verify = importlib.util.module_from_spec(spec); spec.loader.exec_module(verify)


def image_source_hashes(pin, repository=ROOT):
    # A newer mutable engine checkout must not invalidate a historical image
    # test. Match the immutable image to its exact, locally known Git source.
    commit = pin.get('distribution_commit', '')
    if not isinstance(commit, str) or not re.fullmatch(r'(?:[0-9a-f]{40}|[0-9a-f]{64})', commit):
        raise ValueError('Image must name a full, locally known distribution commit')
    paths = {'engine_sha256': 'distro/installer-engine/engine.py',
             'wrapper_sha256': 'distro/iso/profile/airootfs/usr/bin/platinum-reality-install'}
    hashes = {}
    for key, path in paths.items():
        blob = subprocess.check_output(['git', '-C', str(repository), 'show', commit+':'+path])
        hashes[key] = hashlib.sha256(blob).hexdigest()
        if hashes[key] != pin.get(key):
            raise ValueError('Image manifest differs from the named tracked '+key)
    return hashes


def prepare_occupied_target(remote):
    # Only the controller's newly created disposable /dev/vda. The baseline
    # full-disk hash is taken AFTER this explicit fixture preparation.
    remote('sudo -n sgdisk --zap-all /dev/vda && '
           'sudo -n sgdisk --new=1:0:+1G --typecode=1:0700 --change-name=1:VM_DATA /dev/vda && '
           'sudo -n udevadm settle --timeout=30 && '
           'sudo -n mkfs.vfat -n VM_DATA /dev/vda1 && '
           'sudo -n udevadm trigger --action=change /sys/class/block/vda1 && '
           'sudo -n udevadm settle --timeout=30', 'prepare-occupied-vm-target')
    columns='PATH,TYPE,MODEL,SIZE,RM,TRAN,FSTYPE,LABEL,MOUNTPOINTS,MAJ:MIN,RO'
    remote('lsblk --version','occupied-lsblk-version')
    remote('lsblk --json --bytes --paths --output '+columns,'occupied-device-flat')
    remote('lsblk --json --tree --bytes --paths --output '+columns,'occupied-device-tree')


def check_occupied_plan(plan):
    parts = plan.get('existing_partitions')
    if (not plan.get('ok') or plan.get('erases') != ['/dev/vda'] or not isinstance(parts, list) or
            len(parts) != 1 or parts[0].get('path') != '/dev/vda1' or parts[0].get('label') != 'VM_DATA' or
            parts[0].get('fstype') != 'vfat' or not any('every listed partition' in w for w in plan.get('warnings',[]))):
        raise ValueError('Occupied-disk plan did not explicitly describe the existing test partition')


def mounted_child_refusal(executable, remote, receipt):
    mounted=False
    try:
        remote('sudo -n mkdir /run/pr-occupied-refusal && '
               'sudo -n mount -o ro /dev/vda1 /run/pr-occupied-refusal', 'mount-occupied-readonly')
        mounted=True
        raw=remote(executable+'install --config /run/pr-test/config.json --confirm-erase /dev/vda --progress-fd 1',
                   'mounted-child-install-refusal', input=b'', expected=2)
        events=[json.loads(line) for line in raw.splitlines()]
        if (not events or events[-1].get('result')!='error' or
                'unmounted whole disk' not in events[-1].get('message','') or
                any(e.get('step') in ('partition','format','mount','packages','configure','bootloader','finish') for e in events)):
            raise ValueError('Mounted-child installation was not explicitly refused before writes')
        receipt['mounted_child_install_refusal']={'message':events[-1]['message'],
            'credential_input':'empty stdin', 'mount':'read-only FAT fixture',
            'disk_hash':'Required unchanged by enclosing occupied scenario'}
    finally:
        if mounted:
            remote('sudo -n umount /run/pr-occupied-refusal && '
                   'sudo -n rmdir /run/pr-occupied-refusal', 'unmount-occupied-readonly')


def refusal_checks(scenario, executable, remote, receipt):
    # Only a newly created invocation-owned target reaches this branch.
    if scenario not in ('small','occupied','bios'):
        raise ValueError('Unknown refusal scenario')
    if scenario == 'occupied':
        prepare_occupied_target(remote)
    before = remote('sudo -n sha256sum /dev/vda', 'scenario-hash-before').split()[0]
    if scenario == 'occupied':
        plan = json.loads(remote(executable+'plan --config /run/pr-test/config.json --json', 'occupied-plan'))
        check_occupied_plan(plan)
        receipt['occupied_plan'] = plan
        receipt['gui_erase_confirmation'] = 'NOT TESTED: this is engine-only plan evidence'
        mounted_child_refusal(executable,remote,receipt)
    elif scenario in ('small', 'bios'):
        result = remote(executable+'install --config /run/pr-test/config.json --confirm-erase /dev/vda --progress-fd 1',
                        'scenario-refusal', input=b'', expected=2)
        events = [json.loads(line) for line in result.splitlines()]
        wanted = 'at least 16 GiB' if scenario == 'small' else 'UEFI'
        if (not events or events[-1].get('result') != 'error' or wanted not in events[-1].get('message','') or
                any(event.get('step') in ('partition','format','mount','packages','configure','bootloader','finish') for event in events)):
            raise ValueError('Expected explicit pre-write '+scenario+' refusal')
        receipt['refusal_message'] = events[-1]['message']
    else:
        raise ValueError('Unknown refusal scenario')
    after = remote('sudo -n sha256sum /dev/vda', 'scenario-hash-after').split()[0]
    if not re.fullmatch(r'[0-9a-f]{64}', before) or before != after:
        raise ValueError('Read-only refusal/plan changed the full target disk')
    receipt.update(refusal_scenario=scenario, full_target_hash=before,
                   refusal_guards='PASS: full target unchanged; no credential input', ok=True)


def key(guest, codes):
    keys = [{'type': 'qcode', 'data': code} for code in codes.split('+')]
    guest.qmp('input-send-event', {'events': [
        {'type': 'key', 'data': {'key': code, 'down': True}} for code in keys]})
    try:
        time.sleep(.025)
    finally:
        # Explicit ordered release, including modifiers; do not depend on
        # QEMU's later VM-time send-key release timer while driving dialogs.
        guest.qmp('input-send-event', {'events': [
            {'type': 'key', 'data': {'key': code, 'down': False}} for code in reversed(keys)]})
    time.sleep(.04)


def type_text(guest, value):
    punctuation = {' ': 'spc', '/': 'slash', '-': 'minus', '_': 'shift+minus', '.': 'dot',
                   "'": 'apostrophe', '"': 'shift+apostrophe', ';': 'semicolon', ':': 'shift+semicolon',
                   '=': 'equal', '>': 'shift+dot', '@': 'shift+2'}
    for char in value:
        if char in punctuation:
            key(guest, punctuation[char])
        elif char.isascii() and char.isalnum():
            key(guest, ('shift+' if char.isupper() else '')+char.lower())
        else:
            raise ValueError('Unsupported synthetic QMP key character')


def wait_live(guest, deadline):
    started = time.monotonic()
    captures = []
    next_capture = started
    while time.monotonic() < deadline:
        if guest.process.poll() is not None:
            raise RuntimeError('Owned live VM exited')
        try:
            log = (guest.path/'serial.log').read_text(errors='replace')
        except FileNotFoundError:
            log = ''  # QMP can become ready before QEMU opens the serial log.
        if verify.ready_marker(log):
            (guest.path/'live-boot-captures.json').write_text(json.dumps(captures, indent=2)+'\n')
            return
        # Bounded, input-free observations of actual firmware/Plymouth startup.
        # At most 30 frames over the first minute; readiness is still independent.
        now = time.monotonic()
        if now >= next_capture and len(captures) < 30 and now-started < 60:
            name = 'live-boot-%02d.ppm' % len(captures)
            guest.qmp('screendump', {'filename': str(guest.path/name)})
            captures.append({'file': name, 'elapsed_seconds': round(now-started, 3)})
            next_capture = now+2
        time.sleep(.5)
    raise TimeoutError('Live desktop did not become ready')


def greeter_visible(text, greeter='stock'):
    value = ' '.join(text.casefold().split())
    if greeter == 'platinum':
        return ('welcome, test user' in value and 'enter your password' in value and
                ('log in' in value or 'login' in value))
    if greeter != 'stock':
        return False
    return 'test user' in value and 'enter password' in value


def greeter_text(screenshot, greeter):
    text = subprocess.check_output(['tesseract', str(screenshot), 'stdout', '--psm', '11'],
                                   stderr=subprocess.DEVNULL, text=True)
    value = ' '.join(text.casefold().split())
    if greeter == 'platinum' and 'welcome, test user' in value and 'enter your password' in value:
        # Sparse full-screen OCR drops the small title in the striped bar.
        # Read that exact, reviewed title rectangle separately; the same strict
        # account/password/title predicates still gate credential entry.
        from PIL import Image
        with Image.open(screenshot) as image:
            if image.size not in ((1280, 800), (1024, 768)):
                return text
            width, height = image.size
            left, top = width//2-24, height//2-130
            title = image.crop((left, top, left+48, top+16)).resize((192, 64), Image.Resampling.LANCZOS)
            path = Path(screenshot).with_name('login-title-ocr.png'); title.save(path)
        text += '\n'+subprocess.check_output(['tesseract', str(path), 'stdout', '--psm', '7'],
                                            stderr=subprocess.DEVNULL, text=True)
    return text


def desktop_visible(text):
    import re
    words = set(re.findall(r'[a-z]+', text.casefold()))
    return len(words & {'file','edit','view','window','special','help'}) >= 5 and ('desktop' in words or ('finder' in words and ('trash' in words or {'hard','disk'} <= words))) and 'enter password' not in text.casefold()


def unlock_visible(text, ui):
    import re
    value = ''.join(re.findall(r'[a-z]+', text.casefold()))
    if ui == 'plymouth':
        return 'enteryourpasswordtostartup' in value
    if ui == 'plain':
        return 'passphrase' in value and ('cryptroot' in value or 'harddisk' in value)
    return False


def graphical_unlock_visible(screenshot, reference):
    # The theme uses an original bitmap font which OCR misreads. Match the
    # actual immutable image's opaque dialog pixels at its reviewed geometry;
    # never relax a password prompt to a fuzzy text guess.
    from PIL import Image
    with Image.open(screenshot) as image, Image.open(reference) as expected:
        if image.size not in ((1280, 800), (1024, 768)):
            return False  # Firmware may use a different size before KMS loads.
        width, height = image.size
        scale = min(width//512, height//342)
        left = (width-512*scale)//2+106*scale
        top = (height-342*scale)//2+202*scale
        size = (300*scale, 68*scale)
        if expected.size != size:
            raise ValueError('Wrong image-pinned unlock reference; no password typed')
        actual = image.convert('RGB').crop((left, top, left+size[0], top+size[1]))
        expected = expected.convert('RGBA')
        actual_pixels, expected_pixels = actual.load(), expected.load()
        opaque = 0
        for y in range(size[1]):
            for x in range(size[0]):
                pixel = expected_pixels[x, y]
                if pixel[3] == 255:
                    opaque += 1
                    if actual_pixels[x, y] != pixel[:3]:
                        return False
        return opaque > 1000


def login_point(screenshot, text, greeter='stock'):
    if not greeter_visible(text, greeter):
        raise ValueError('Password entry requires the real test-account greeter')
    with Path(screenshot).open('rb') as source:
        kind, size, depth = [source.readline().strip() for _ in range(3)]
        accepted = {b'1280 800'} if greeter == 'stock' else {b'1280 800', b'1024 768'}
        if kind != b'P6' or size not in accepted or depth != b'255':
            raise ValueError('Unrecognized greeter geometry; no password typed')
    if greeter == 'platinum':
        width, height = map(int, size.split())
        return {'mode': 'keyboard', 'width': width, 'height': height,
                'fixture': 'Platinum SDDM one-account password-focus contract; real greeter screenshot retained'}
    return {'x': 765, 'y': 489, 'width': 1280, 'height': 800,
            'mode': 'click',
            'fixture': 'SDDM stock Maldives; reviewed install-20261029T092631Z/login.png'}


def submit_login(guest, password, action):
    if action['mode'] == 'click':
        x, y, width, height = (action[key] for key in ('x', 'y', 'width', 'height'))
        guest.qmp('input-send-event', {'events': [
            {'type': 'abs', 'data': {'axis': 'x', 'value': round(32767*x/(width-1))}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': round(32767*y/(height-1))}},
            {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
        guest.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
    elif action['mode'] != 'keyboard':
        raise ValueError('Unknown login action; no password typed')
    type_text(guest, password)
    key(guest, 'ret')


def collect_live_logs(guest):
    # tar --wildcards does not glob source files in create mode. Enumerate
    # only the known log names explicitly inside the owned guest instead.
    script = """import sys,tarfile
from pathlib import Path
root=Path('/home/liveuser/.local/state/platinum-reality/session-runs')
count=0
with tarfile.open(fileobj=sys.stdout.buffer,mode='w|') as archive:
 for run in sorted(root.iterdir()):
  if not run.is_dir() or run.is_symlink(): continue
  for name in ('supervisor.txt','backend.txt','desktop.txt','session.json'):
   item=run/name
   if item.is_file() and not item.is_symlink():
    archive.add(item,arcname=str(item.relative_to(root)),recursive=False)
    count+=1
if not count: raise SystemExit('No owned live session logs found')
"""
    archive = guest.path/'live-session-logs.tar'
    with archive.open('wb') as output, (guest.path/'live-log-collection.stderr').open('wb') as error:
        guest.ssh('python3 -I -c '+shlex.quote(script), stdout=output, stderr=error, timeout=15)
    with tarfile.open(archive) as data:
        data.extractall(guest.path/'live-session-logs', filter='data')


def boot_proof(guest, password, receipt):
    path = guest.path
    greeter = receipt.get('greeter', 'stock')
    unlock_ui = receipt.get('unlock_ui', 'plain')
    collect_live_logs(guest)
    shutil.copyfile(path/'command.json', path/'live-command.json')
    guest.stop()
    shutil.move(path/'serial.log', path/'live-serial.log')
    # Boot the exact installed system: no ISO, fixture service, SSH key,
    # autologin, PAM bypass or target configuration changes.
    # QEMU's UART makes Plymouth choose a serial/text console. The graphical
    # installed-boot fixture has no UART; use QMP screenshots for observations.
    # Live boot retains serial for its readiness marker and fixture diagnostics.
    guest.start(uefi=True, network=False, target=path/'target.qcow2', installed=True,
                serial=unlock_ui != 'plymouth', gpu=receipt.get('gpu', 'virtio-vga'),
                outputs=receipt.get('requested_output_capacity', 1))
    observe_installed_boot(guest, password, receipt)


def observe_installed_boot(guest, password, receipt, stem=''):
    """Observe real unlock/login without transport, reset or authentication edits."""
    if stem not in ('', 'reboot-'):
        raise ValueError('Unsupported installed capture prefix')
    path = guest.path
    greeter = receipt.get('greeter', 'stock')
    unlock_ui = receipt.get('unlock_ui', 'plain')
    observed={'scope':'One owned VM run, host monotonic offsets from QEMU start; includes controller/input waiting, not intrinsic boot performance'}
    receipt['installed_boot_observations']=observed
    receipt['installed_virtual_serial'] = unlock_ui != 'plymouth'
    deadline = time.monotonic()+180
    if receipt['encrypted']:
        while time.monotonic() < deadline:
            guest.qmp('screendump', {'filename': str(path/(stem+'unlock.ppm'))})
            text = (subprocess.check_output(['tesseract', str(path/(stem+'unlock.ppm')), 'stdout', '--psm', '6'],
                                           stderr=subprocess.DEVNULL, text=True).lower()
                    if unlock_ui == 'plain' else '')
            recognized = (graphical_unlock_visible(path/(stem+'unlock.ppm'), path/'expected-unlock-dialog.png')
                          if unlock_ui == 'plymouth' else unlock_visible(text, unlock_ui))
            if recognized:
                observed['unlock_prompt_seconds']=round(time.monotonic()-guest.started,3)
                # Exactly one attempt, only after seeing the actual unlock prompt.
                type_text(guest, password)
                guest.qmp('screendump', {'filename': str(path/(stem+'unlock-typed.ppm'))})
                key(guest, 'ret')
                observed['unlock_submitted_seconds']=round(time.monotonic()-guest.started,3)
                receipt['unlock_prompt'] = 'Observed '+unlock_ui+'; one synthetic credential submitted by owned QMP'
                if unlock_ui == 'plymouth':
                    receipt['unlock_recognition'] = 'Exact opaque pixels of image-pinned original dialog at reviewed geometry'
                break
            time.sleep(1)
        else:
            raise TimeoutError('Installed UKI did not show a recognizable LUKS prompt; no password typed')
    deadline = time.monotonic()+180
    while True:
        guest.qmp('screendump', {'filename': str(path/(stem+'login.ppm'))})
        text = greeter_text(path/(stem+'login.ppm'), greeter)
        if greeter_visible(text, greeter):
            observed['greeter_prompt_seconds']=round(time.monotonic()-guest.started,3)
            break
        if time.monotonic() > deadline:
            raise TimeoutError('Installed disk did not reach the real SDDM greeter')
        time.sleep(1)
    time.sleep(2)
    guest.qmp('screendump', {'filename': str(path/(stem+'login.ppm'))})
    text = greeter_text(path/(stem+'login.ppm'), greeter)
    receipt['installed_disk_boot'] = 'PASS: all ISOs detached; real password greeter observed'
    # Stock baseline keeps its reviewed click. The explicitly selected Platinum
    # theme already focuses the password; do not reuse the old click geometry.
    # Recognize the selected theme/account before sending a synthetic credential.
    point = login_point(path/(stem+'login.ppm'), text, greeter)
    (path/(stem+'login-action.json')).write_text(json.dumps(point)+'\n')
    submit_login(guest, password, point)
    observed['account_submitted_seconds']=round(time.monotonic()-guest.started,3)
    deadline = time.monotonic()+90
    while True:
        guest.qmp('screendump', {'filename': str(path/(stem+'installed-desktop.ppm'))})
        text = subprocess.check_output(['tesseract', str(path/(stem+'installed-desktop.ppm')), 'stdout', '--psm', '11'],
                                       stderr=subprocess.DEVNULL, text=True)
        if desktop_visible(text):
            observed['desktop_observed_seconds']=round(time.monotonic()-guest.started,3)
            break
        if time.monotonic() > deadline:
            raise TimeoutError('Password login did not reach installed Platinum desktop')
        time.sleep(1)
    time.sleep(1)
    guest.qmp('screendump', {'filename': str(path/(stem+'installed-desktop.ppm'))})
    receipt['password_login'] = 'PASS'
    receipt['installed_display_captures'] = guest.capture_displays(stem+'installed-head')
    receipt['password_login_evidence'] = 'Real SDDM authentication followed by installed Platinum desktop; ISOs detached; no target authentication changes'
    print('Installed-disk password login and desktop readiness passed.', flush=True)


def restart_and_repeat_login(guest, password, receipt, gui=None):
    """Request the installed desktop's normal Restart; never QMP-reset a pass."""
    if gui is None:
        source = Path(__file__).with_name('gui-drive.py')
        spec = importlib.util.spec_from_file_location('installed_gui', source)
        gui = importlib.util.module_from_spec(spec); spec.loader.exec_module(gui)
    # The controller's reviewed one-output geometry only. Validate the actual
    # frame before sending even the blank-desktop activation click.
    gui.capture(guest, 'reboot-before', region=(0, 0, 1280, 800))
    gui.click(guest, 600, 740)
    words = gui.capture(guest, 'reboot-finder-menu', region=(0, 0, 1280, 26))
    special = gui.matches(words, 'Special')
    if len(special) != 1 or not 0 <= special[0]['y'] < 26:
        raise ValueError('No unique Finder Special menu; no restart requested')
    point = special[0]
    gui.click(guest, point['x'], point['y'])
    left = max(0, point['left'] - 15)
    words = gui.capture(guest, 'reboot-special-menu', region=(left, 26, 240, 320))
    positions = [gui.matches(words, name) for name in ('Sleep', 'Restart', 'Shut Down')]
    if (any(len(found) != 1 for found in positions) or
            not positions[0][0]['y'] < positions[1][0]['y'] < positions[2][0]['y'] or
            any(not left <= found[0]['x'] < left+220 or not 26 <= found[0]['y'] < 300 for found in positions)):
        raise ValueError('Unrecognized actual power menu; no restart requested')
    restart = positions[1][0]
    repeat = {key: receipt[key] for key in ('encrypted', 'greeter', 'unlock_ui', 'gpu', 'requested_output_capacity')}
    repeat['request'] = 'Actual installed Finder Special menu Restart via QMP pointer; no force reset, SSH or target edits'
    receipt['reboot_login'] = repeat
    gui.click(guest, restart['x'], restart['y'])
    # A fresh encrypted prompt after an authenticated desktop establishes a
    # new startup; credentials are submitted only after the normal recognizers.
    observe_installed_boot(guest, password, repeat, stem='reboot-')
    repeat['ok'] = repeat.get('password_login') == 'PASS'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--gpu', choices=GPUS, default='virtio-vga')
    parser.add_argument('--outputs', type=int, choices=(1, 2), default=1)
    parser.add_argument('--iso-media', choices=('cdrom', 'usb'), default='cdrom', help='Attach the live ISO as a CD drive or as a USB stick')
    parser.add_argument('--gui-version', choices=('legacy','37'), default='legacy', help='Explicit installer layout; 37 requires the source-pinned new native installer')
    parser.add_argument('--gui', action='store_true', help='Drive the actual image installer GUI (requires --image-engine --boot-proof)')
    parser.add_argument('--erase-return-only', action='store_true', help='Occupied GUI cancellation control only; stop after Return cancels with unchanged full-disk hash, never install or boot target')
    parser.add_argument('--repeat-login', action='store_true', help='After GUI installation and first login, use the installed Finder Restart and repeat encrypted/password login')
    parser.add_argument('--occupied-gui', action='store_true', help='GUI proof with a synthetic existing partition; require its actual erase warning before confirmation')
    parser.add_argument('--verify-erase-return', action='store_true', help='With occupied GUI, require Return to cancel and preserve the full disk hash before explicit erase')
    parser.add_argument('--collect-boot-metrics', action='store_true', help='Collect read-only live-VM systemd timing/blame/critical-chain; no installed transport or host query')
    parser.add_argument('--observe-displays', action='store_true', help='Live VM only: retain actual backend/topology outputs, no installation or display setting changes')
    parser.add_argument('--open-monitors', action='store_true', help='With --observe-displays, open the real Monitors panel through its Apple menu; no settings changed')
    parser.add_argument('--enable-second-display', action='store_true', help='With two-output live observation and Monitors UI, turn on the detected off monitor through its real checkbox and Keep')
    parser.add_argument('--image-engine', action='store_true', help='Test the immutable ISO engine/wrapper instead of injecting engine source (R3)')
    parser.add_argument('--refusal-scenario', choices=('small','occupied','bios'),
                        help='Engine-only VM proof; never performs an installation')
    parser.add_argument('--lease-wait', type=float, default=0, help='Wait at most this many seconds for the shared heavy-work lease (max 1800)')
    parser.add_argument('--interrupt-retry', action='store_true', help='Explicit owned-VM package-write interruption, preserved same disk, destructive reinstall, detached boot and password login')
    parser.add_argument('--unencrypted', action='store_true')
    parser.add_argument('--diagnostic-hold', action='store_true', help='Retain only this owned failing guest for at most 5 minutes of inspection')
    parser.add_argument('--boot-proof', action='store_true', help='Detach both ISOs, boot installed disk and validate encrypted/password login automatically against the reviewed stock greeter')
    parser.add_argument('--greeter', choices=('stock', 'platinum'), default='stock',
                        help='Explicit expected real SDDM theme; Platinum uses its default keyboard focus, never stock click coordinates')
    parser.add_argument('--unlock-ui', choices=('plain', 'plymouth'), default='plain',
                        help='Explicit expected encrypted-unlock UI; never guess before entering the synthetic credential')
    args = parser.parse_args(argv)
    # The live ISO as the guest sees it: the only usb-storage disk, or the first CD drive.
    boot_medium = '/dev/sda' if args.iso_media == 'usb' else '/dev/sr0'
    if args.gui_version != 'legacy' and not args.gui:
        parser.error('--gui-version requires --gui')
    if args.interrupt_retry and (not args.image_engine or not args.boot_proof or args.unencrypted or args.gui or args.refusal_scenario or args.observe_displays or args.gpu!='virtio-vga' or args.outputs!=1):
        parser.error('--interrupt-retry requires encrypted --image-engine --boot-proof, one virtio output, and no GUI/refusal/display-observation scenario')
    if args.erase_return_only and not (args.gui and args.image_engine and args.occupied_gui and args.verify_erase_return and not args.boot_proof and not args.repeat_login and not args.unencrypted and not args.refusal_scenario and not args.observe_displays and args.unlock_ui == 'plain'):
        parser.error('--erase-return-only requires encrypted occupied GUI/verified Return/image engine, with no boot, repeat, refusal, display or graphical-unlock scenario')
    if args.gui and not (args.image_engine and (args.boot_proof or args.erase_return_only)):
        parser.error('--gui requires --image-engine --boot-proof')
    if args.repeat_login and not (args.gui and args.boot_proof and args.image_engine and not args.unencrypted):
        parser.error('--repeat-login requires encrypted --gui --image-engine --boot-proof')
    if args.occupied_gui and not args.gui:
        parser.error('--occupied-gui requires --gui')
    if args.verify_erase_return and not args.occupied_gui:
        parser.error('--verify-erase-return requires --occupied-gui')
    if args.enable_second_display and not (args.observe_displays and args.open_monitors and args.outputs==2):
        parser.error('--enable-second-display requires --observe-displays --open-monitors --outputs 2')
    if args.open_monitors and not args.observe_displays:
        parser.error('--open-monitors requires --observe-displays')
    if args.observe_displays and (args.gui or args.boot_proof or args.refusal_scenario or args.unlock_ui!='plain'):
        parser.error('Display observation is live-only and cannot install or perform refusal/boot-proof/graphical-unlock scenarios')
    if args.gui and (args.gpu != 'virtio-vga' or args.outputs != 1):
        parser.error('GUI driver currently supports only the reviewed one-output virtio-vga fixture')
    if args.unlock_ui == 'plymouth' and not args.image_engine:
        parser.error('--unlock-ui plymouth requires --image-engine for its pinned visual reference')
    if args.refusal_scenario and (args.gui or args.boot_proof or args.unlock_ui != 'plain'):
        parser.error('Refusal scenarios cannot be combined with GUI, boot-proof or graphical unlock')
    if os.geteuid() == 0:
        raise SystemExit('Run the host controller as an ordinary user only')
    if shutil.disk_usage(ROOT).free < 20 * 1024**3:
        raise SystemExit('Preserving host free space: need at least 20 GiB before another owned install test')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    path = ROOT/'build/distro'/('install-'+stamp)
    # The password is generated in memory, passed over SSH stdin, and never
    # included in the fixture ISO, argv, QMP transcript, logs or receipts.
    password = 'PrVm'+secrets.token_hex(12)
    config = {'version': 1, 'disk': '/dev/vda', 'hostname': 'pr-installed', 'timezone': 'UTC',
              'locale': 'en_US.UTF-8', 'keymap': 'us', 'user': {'name': 'tester', 'full_name': 'Test User'},
              'encrypt': not args.unencrypted, 'network': 'iwd-networkd'}
    if args.gui:
        config.update(locale='en_GB.UTF-8', timezone='Europe/London', keymap='uk')
    receipt = {'ok': False, 'installed': False, 'encrypted': config['encrypt'], 'iso_sha256': sha256(args.iso),
               'greeter': args.greeter,
               'unlock_ui': args.unlock_ui, 'requested_choices': {k: config[k] for k in ('locale', 'timezone', 'keymap')},
               'controller_sha256': sha256(Path(__file__)), 'guest_helper_sha256': sha256(Path(__file__).with_name('owned.py')),
               'installed_disk_boot': 'NOT TESTED', 'password_login': 'NOT TESTED',
               'gpu': args.gpu, 'requested_output_capacity': args.outputs,
               'guest_output_activity': 'NOT VERIFIED: separate Monitors/UI evidence required'}
    receipt['live_boot_metrics_requested']=args.collect_boot_metrics
    receipt['erase_return_only_requested']=args.erase_return_only
    receipt['verify_erase_return_requested']=args.verify_erase_return
    receipt['interrupted_retry_requested']=args.interrupt_retry
    with Lease(wait=args.lease_wait):
        path.mkdir(mode=0o700)
        guest = Guest(path, username='liveuser')
        try:
            snapshot = path/'controller-source'; snapshot.mkdir()
            for name in ('install-proof.py', 'owned.py', 'display_profiles.py', 'verify-iso.py', 'gui-drive.py', 'read-installed.py'):
                shutil.copyfile(Path(__file__).with_name(name), snapshot/name)
            if args.gui_version == '37':
                shutil.copyfile(Path(__file__).with_name('gui-drive-v37.py'),snapshot/'gui-drive-v37.py')
            if args.interrupt_retry:
                for name in ('package-write-probe.py','interrupted_retry.py'):
                    shutil.copyfile(Path(__file__).with_name(name),snapshot/name)
                receipt['interruption_source_sha256']={name:sha256(snapshot/name) for name in ('package-write-probe.py','interrupted_retry.py')}
            if args.collect_boot_metrics:
                collector=Path(__file__).with_name('boot-metrics.py')
                shutil.copyfile(collector,snapshot/collector.name)
                receipt['boot_metrics_collector_sha256']=sha256(collector)
            if args.observe_displays:
                collector=Path(__file__).with_name('read-displays.py')
                shutil.copyfile(collector,snapshot/collector.name)
                receipt['display_collector_sha256']=sha256(collector)
                if args.open_monitors:
                    shutil.copyfile(Path(__file__).with_name('monitors-ui.py'),snapshot/'monitors-ui.py')
            assert sha256(snapshot/'install-proof.py') == receipt['controller_sha256']
            assert sha256(snapshot/'owned.py') == receipt['guest_helper_sha256']
            run(['qemu-img', 'create', '-f', 'qcow2', path/'target.qcow2',
                 '8G' if args.refusal_scenario == 'small' else '16G'])
            run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', path/'key'])
            seed = path/'fixture'; seed.mkdir()
            if args.collect_boot_metrics:
                shutil.copyfile(snapshot/'boot-metrics.py',seed/'boot-metrics.py')
            if args.interrupt_retry:
                shutil.copyfile(snapshot/'package-write-probe.py',seed/'package-write-probe.py')
            if args.observe_displays:
                shutil.copyfile(snapshot/'read-displays.py',seed/'read-displays.py')
            readback = Path(__file__).with_name('read-installed.py')
            shutil.copyfile(readback, seed/'read-installed.py')
            receipt['readback_helper_sha256'] = sha256(readback)
            for name, content in [('config.json', config), ('boot-config.json', config | {'disk': boot_medium})]:
                (seed/name).write_text(json.dumps(content)+'\n')
            (seed/'key.pub').write_bytes((path/'key.pub').read_bytes())
            source = ROOT/'distro/installer-engine/engine.py'
            if not args.image_engine:
                (seed/'engine.py').write_bytes(source.read_bytes())
            if not args.image_engine:
                receipt['engine_sha256'] = sha256(source)
            (seed/'setup.sh').write_text('''#!/bin/sh
set -eu
test -d /run/archiso/bootmnt
test "$(systemd-detect-virt --vm)" = kvm || test "$(systemd-detect-virt --vm)" = qemu
install -d -m 700 -o liveuser -g liveuser /home/liveuser/.ssh
install -m 600 -o liveuser -g liveuser /run/pr-test/key.pub /home/liveuser/.ssh/authorized_keys
systemctl start sshd
{
    printf '\\nPR_INSTALL_FIXTURE_NETWORK_BEGIN\\n'
    ip -brief address || true
    ip -4 route || true
    ss -lnt || true
    printf 'PR_INSTALL_FIXTURE_NETWORK_END\\n'
} > /dev/ttyS0 2>&1
printf '\\nPR_INSTALL_FIXTURE_READY\\n' > /dev/ttyS0
''')
            run(['genisoimage', '-quiet', '-output', path/'fixture.iso', '-volid', 'PRTEST', '-joliet', '-rock', seed])
            unit = '''[Unit]
Description=Owned VM install-test provisioning only
ConditionPathExists=/run/archiso/bootmnt
After=local-fs.target systemd-udevd.service
[Service]
Type=oneshot
ExecStartPre=/usr/bin/udevadm settle --timeout=30
ExecStartPre=/usr/bin/mkdir -p /run/pr-test
ExecStartPre=/usr/bin/mount -o ro /dev/disk/by-label/PRTEST /run/pr-test
ExecStart=/usr/bin/sh /run/pr-test/setup.sh
TimeoutStartSec=60
RemainAfterExit=yes
'''
            # Standard systemd-debug-generator credentials, supplied by OUR VM
            # controller only, not baked into the immutable R1 ISO. Source:
            # https://www.freedesktop.org/software/systemd/man/latest/systemd-debug-generator.html
            units = {'systemd.extra-unit.pr-install-test.service': unit,
                     'systemd.unit-dropin.multi-user.target': '[Unit]\nWants=pr-install-test.service\n'}
            guest.start(iso=args.iso, iso_media=args.iso_media, uefi=args.refusal_scenario != 'bios', target=path/'target.qcow2', fixture=path/'fixture.iso',
                        restricted=True, test_units=units, gpu=args.gpu, outputs=args.outputs)
            receipt['display_profile'] = guest.display_profile
            receipt['qemu_version'] = guest.qmp('query-version')
            wait_live(guest, time.monotonic()+240)
            receipt['live_desktop_observed_seconds']=round(time.monotonic()-guest.started,3)
            guest.qmp('screendump', {'filename': str(path/'live.ppm')})
            receipt['live_display_captures'] = guest.capture_displays('live-head')
            print('Owned live VM ready; waiting for VM-supplied nonsecret fixture provisioning.', flush=True)
            deadline = time.monotonic()+30
            while 'PR_INSTALL_FIXTURE_READY' not in (path/'serial.log').read_text(errors='replace'):
                if time.monotonic() > deadline:
                    guest.qmp('screendump', {'filename': str(path/'setup-failed.ppm')})
                    raise TimeoutError('Disposable fixture console setup did not complete')
                time.sleep(.5)
            guest.wait_ssh()
            collect_live_logs(guest)

            def remote(command, name, *, input=None, expected=0):
                with (path/(name+'.jsonl')).open('wb') as out, (path/(name+'.stderr')).open('wb') as err:
                    try:
                        result = guest.ssh(command, input=input, stdout=out, stderr=err, timeout=1800)
                        code = result.returncode
                    except subprocess.CalledProcessError as error:
                        code = error.returncode
                if code != expected:
                    raise RuntimeError(name+' returned '+str(code)+'; see private VM evidence')
                return (path/(name+'.jsonl')).read_text()

            if args.collect_boot_metrics:
                receipt['live_boot_metrics']=json.loads(remote(
                    'python3 -I /run/pr-test/boot-metrics.py --role live','live-boot-metrics'))
                if receipt['live_boot_metrics'].get('ok') is not True:
                    raise RuntimeError('Requested live boot metrics did not complete; no timing pass')

            if args.image_engine:
                image = json.loads(remote('cat /opt/platinum-reality/image.json', 'image'))
                pin = image['native_source']
                pinned = image_source_hashes(pin)
                engine_hash = remote('sha256sum /usr/lib/platinum-reality/installer-engine', 'image-engine-hash').split()[0]
                wrapper_hash = remote('sha256sum /usr/bin/platinum-reality-install', 'image-wrapper-hash').split()[0]
                assert engine_hash == pinned['engine_sha256']
                assert wrapper_hash == pinned['wrapper_sha256']
                receipt['engine_sha256'] = engine_hash
                receipt['wrapper_sha256'] = wrapper_hash
                receipt['image_source_verification'] = 'Actual immutable engine/wrapper match full locally known distribution Git commit'
                remote('test -x /usr/bin/platinum-reality-installer && test -f "/home/liveuser/Desktop/Install Platinum Reality.desktop"', 'image-gui-present')
                receipt['native_source_commit'] = pin['commit']
                receipt['distribution_commit'] = pin['distribution_commit']
                receipt['engine_execution'] = 'Immutable ISO live-only sudo wrapper; no fixture engine supplied'
                remote("test -x '/home/liveuser/Desktop/Install Platinum Reality.desktop'", 'image-launcher-mode')
                remote("python3 -I -c "+shlex.quote(
                    "from pathlib import Path; import json; t=Path('/usr/share/plymouth/themes/platinum-reality'); "
                    "assert '@' not in (t/'platinum-reality.script').read_text(); "
                    "assert (t/'bullet-8.png').is_file(); "
                    "assert json.loads((t/'manifest.json').read_text())['icon']=='original'"), 'live-theme-check')
                if args.unlock_ui == 'plymouth':
                    reference = path/'expected-unlock-dialog.png'
                    with reference.open('wb') as output:
                        guest.ssh('cat /usr/share/plymouth/themes/platinum-reality/dialog-2.png', stdout=output)
                    receipt['unlock_reference_sha256'] = sha256(reference)
                executable = '/usr/bin/platinum-reality-install '
            else:
                receipt['engine_execution'] = 'R2 read-only fixture source on the original live image'
                executable = 'sudo -n python3 -I /run/pr-test/engine.py '
            disks = json.loads(remote(executable+'disks --json', 'disks'))
            assert next(d for d in disks if d['path'] == boot_medium)['is_boot_medium'] is True
            if args.refusal_scenario:
                refusal_checks(args.refusal_scenario, executable, remote, receipt)
                return  # finally still stops our VM and writes the scoped result.
            if args.observe_displays:
                observed=json.loads(remote('python3 -I /run/pr-test/read-displays.py','actual-displays'))
                receipt['display_observation']=observed
                spec=importlib.util.spec_from_file_location('read_displays',snapshot/'read-displays.py')
                reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
                assessment=reader.assess(observed,args.outputs)
                receipt['initial_display_assessment']=assessment
                receipt['observed_display_captures']=guest.capture_displays('observed-head')
                receipt['guest_output_activity']='Observed live backend/topology; Monitors UI, installed login, mirror and persistence remain NOT TESTED'
                initial_count=assessment['backend_active_count']
                if initial_count not in (1,2) or not reader.assess(observed,initial_count)['all_requested_outputs_active_and_applied']:
                    raise RuntimeError('Initial active backend and acknowledged desktop layout disagree; no UI input sent')
                if args.open_monitors:
                    spec=importlib.util.spec_from_file_location('monitors_ui',snapshot/'monitors-ui.py')
                    ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)
                    receipt['monitors_ui']=ui.open_panel(guest,observed)
                    receipt['guest_output_activity']='Actual backend/native topology and visible Monitors panel; installed login, mirror and persistence NOT TESTED'
                    if args.enable_second_display:
                        attempt=[0]
                        def recollect():
                            attempt[0]+=1
                            return json.loads(remote('python3 -I /run/pr-test/read-displays.py','display-enable-'+str(attempt[0])))
                        observed,activation=ui.enable_second(guest,observed,recollect)
                        receipt['display_activation']=activation
                        receipt['final_display_observation']=observed
                        receipt['final_display_captures']=guest.capture_displays('enabled-head')
                        receipt['guest_output_activity']=activation['scope']
                assessment=reader.assess(observed,args.outputs)
                receipt['display_assessment']=assessment
                if not assessment['all_requested_outputs_active_and_applied']:
                    raise RuntimeError('Requested active output count or acknowledged native topology differs from the actual guest')
                receipt['ok']=True  # Every requested observation, including the panel, completed.
                return
            if args.occupied_gui:
                prepare_occupied_target(remote)
                receipt['occupied_gui_fixture'] = {'path':'/dev/vda1','label':'VM_DATA','fstype':'vfat',
                                                    'scope':'Synthetic partition on invocation-owned target, prepared before read-only baseline'}
            def disk_hash(name):
                result = remote('sudo -n sha256sum /dev/vda', name)
                return result.split()[0]
            before = disk_hash('hash-before')
            plan = json.loads(remote(executable+'plan --config /run/pr-test/config.json --json', 'plan'))
            assert plan['ok'] and plan['erases'] == ['/dev/vda']
            if args.occupied_gui:
                check_occupied_plan(plan)
                receipt['occupied_plan'] = plan
            assert disk_hash('hash-after-plan') == before
            wrong = remote(executable+'install --config /run/pr-test/config.json --confirm-erase /dev/vdb --progress-fd 1', 'wrong-confirm', expected=2)
            assert 'exactly match' in json.loads(wrong)['message']
            assert disk_hash('hash-after-wrong-confirm') == before
            boot = remote(executable+'install --config /run/pr-test/boot-config.json --confirm-erase '+boot_medium+' --progress-fd 1', 'boot-refusal', expected=2)
            assert 'boot medium' in json.loads(boot)['message']
            assert disk_hash('hash-after-boot-refusal') == before
            receipt['read_only_guards'] = 'PASS: full 16-GiB target hashes unchanged after each refusal/plan'
            if args.interrupt_retry:
                if plan.get('existing_partitions') != []:
                    raise RuntimeError('Interruption evidence requires a freshly blank first-attempt target')
                spec=importlib.util.spec_from_file_location('interrupted_retry',snapshot/'interrupted_retry.py')
                recovery=importlib.util.module_from_spec(spec);spec.loader.exec_module(recovery)
                values={'password':password,'passphrase':password}
                receipt['fresh_blank_target_sha256']=before
                identity=recovery.interrupt(guest,executable,(json.dumps(values)+'\n').encode(),remote,receipt,sha256)
                # Deliberate same-target destructive reinstall, never a new blank disk or repair claim.
                guest.start(iso=args.iso, iso_media=args.iso_media,uefi=True,target=path/'target.qcow2',fixture=path/'fixture.iso',
                            restricted=True,test_units=units,gpu=args.gpu,outputs=args.outputs)
                if recovery.target_identity(guest)!=identity:raise RuntimeError('Retry did not retain the same owned target file')
                wait_live(guest,time.monotonic()+240)
                guest.qmp('screendump',{'filename':str(path/'retry-live.ppm')})
                deadline=time.monotonic()+30
                while 'PR_INSTALL_FIXTURE_READY' not in (path/'serial.log').read_text(errors='replace'):
                    if time.monotonic()>deadline:raise TimeoutError('Retry live fixture did not become ready')
                    time.sleep(.5)
                guest.wait_ssh();collect_live_logs(guest)
                retry_plan=json.loads(remote(executable+'plan --config /run/pr-test/config.json --json','retry-plan'))
                if not retry_plan.get('ok') or retry_plan.get('erases')!=['/dev/vda'] or not retry_plan.get('existing_partitions'):
                    raise RuntimeError('Same-disk retry plan did not describe the interrupted target partitions')
                receipt['interrupted_retry']['retry_plan']=retry_plan
                receipt['interrupted_retry']['explicit_retry_confirmation']='/dev/vda through the immutable engine --confirm-erase'
            if args.gui:
                source = snapshot/'gui-drive-v37.py' if args.gui_version == '37' else Path(__file__).with_name('gui-drive.py')
                if args.gui_version == '37':
                    receipt['gui_support_sha256'] = sha256(snapshot/'gui-drive.py')
                receipt['gui_controller_sha256'] = sha256(source)
                spec = importlib.util.spec_from_file_location('gui_drive', source)
                gui = importlib.util.module_from_spec(spec); spec.loader.exec_module(gui)
                gui.drive(guest, password, receipt, remote, key, type_text)
                if args.erase_return_only:
                    if receipt.get('installed') or receipt.get('erase_return_cancel', {}).get('passed') is not True or not str(receipt.get('gui_cancel_only', '')).startswith('PASS:'):
                        raise RuntimeError('Return-only proof did not attest unchanged cancelled target')
                    if disk_hash('hash-final-return-only') != before:
                        raise RuntimeError('Return-only final full-disk hash changed')
                    receipt['ok'] = True
                    return  # Finally stops guest; no readback, reboot or install accepted.
            else:
                values = {'password': password}
                if config['encrypt']:
                    values['passphrase'] = password
                print('Plan, wrong confirmation and boot-medium refusal passed full-disk hash checks; installing owned target.', flush=True)
                output = remote(executable+'install --config /run/pr-test/config.json --confirm-erase /dev/vda --progress-fd 1',
                                'install', input=(json.dumps(values)+'\n').encode())
                events = [json.loads(line) for line in output.splitlines()]
                assert events[-1] == {'result': 'ok'}
                fractions = [x['fraction'] for x in events if 'fraction' in x]
                assert fractions == sorted(fractions) and fractions[-1] == 1
                receipt['installed'] = True
                if args.interrupt_retry:
                    receipt['interrupted_retry']['reinstall_completed']=True
                print('Engine installation returned ok; installed-disk boot/login remain untested.', flush=True)
            # Read the cleanly unmounted target through a temporary read-only
            # guest mount. No target configuration/authentication edits.
            receipt['installed_readback'] = json.loads(remote(
                'sudo -n python3 -I /run/pr-test/read-installed.py', 'installed-readback', input=password.encode()))
            if args.boot_proof:
                boot_proof(guest, password, receipt)
                if args.repeat_login:
                    restart_and_repeat_login(guest, password, receipt)
            if args.interrupt_retry:
                receipt['interrupted_retry']['ok']=bool(receipt['installed'] and receipt['installed_disk_boot'].startswith('PASS:') and receipt['password_login']=='PASS')
                if not receipt['interrupted_retry']['ok']:raise RuntimeError('Reinstall did not complete every required boot/login stage')
            receipt['ok'] = True
        except BaseException as error:
            receipt['ok'] = False
            receipt['failure'] = type(error).__name__+': '+str(error)
            if guest.process and guest.process.poll() is None and guest.is_live_iso:
                try:
                    collect_live_logs(guest)
                except Exception as diagnostic:
                    receipt['live_log_collection_error'] = type(diagnostic).__name__
            (path/'failure-before-cleanup.json').write_text(json.dumps({'failure': receipt['failure']})+'\n')
            if (args.diagnostic_hold and ((path/'install.jsonl').exists() or args.observe_displays)
                    and guest.process and guest.process.poll() is None):
                print('Owned failing VM retained up to 300 seconds for diagnostics: '+str(path), flush=True)
                deadline = time.monotonic()+300
                while time.monotonic() < deadline and not (path/'finish-diagnostics').exists():
                    time.sleep(1)
            raise
        finally:
            guest.stop()
            for item in path.rglob('*'):
                if item.is_file() and item.suffix in ('.json', '.jsonl', '.stderr', '.log', '.tsv') and item.stat().st_size < 16*1024**2:
                    if password.encode() in item.read_bytes():
                        raise RuntimeError('Synthetic secret appeared in diagnostic artifact; do not publish')
            (path/'result.json').write_text(json.dumps(receipt, indent=2)+'\n')
            (path/'lease-finished.txt').write_text(datetime.now(timezone.utc).isoformat()+'\n')
            print('Owned VM proof result: '+str(path/'result.json'), flush=True)


if __name__ == '__main__':
    main()
