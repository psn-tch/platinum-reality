#!/usr/bin/env python3
"""Platinum Reality installer contract v1: owned QEMU/KVM guests, and USB drives on real computers.

Discovery and planning never partition, format, mount or configure anything.
Passwords belong only on stdin, never configuration files, argv or progress.
"""
import argparse
import fcntl
import configparser
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import time
import traceback
import sys
import tempfile
import secrets
import resource
import shutil
import signal
from contextlib import contextmanager

BOOT_MOUNT = Path('/run/archiso/bootmnt')
REPO = Path('/opt/platinum-reality/repo')
BOOT_ASSETS = Path('/usr/lib/platinum-reality/installer-boot')
BOOT_THEME = Path('/usr/share/plymouth/themes/platinum-reality')
IMAGE_RECORD = Path('/opt/platinum-reality/image.json')
BOOT_ASSET_FILES = {'scripts/boot-splash.py', 'packaging/plymouth/platinum-reality.plymouth',
                    'packaging/plymouth/platinum-reality.script.in'}
MIN_DISK_BYTES = 16 * 1024**3
STEPS = [('validate', 'Check the installation'), ('partition', 'Partition the destination'),
         ('format', 'Create filesystems'), ('mount', 'Mount the destination'),
         ('packages', 'Install Arch Linux and Platinum Reality'), ('configure', 'Configure the system'),
         ('bootloader', 'Prepare startup'), ('finish', 'Finish and unmount')]
TARGET_PACKAGES = ['base', 'linux', 'linux-firmware', 'amd-ucode', 'intel-ucode', 'mesa',
    'platinum-reality', 'sudo', 'mkinitcpio', 'systemd-ukify', 'cryptsetup', 'plymouth', 'sddm', 'xorg-server',
    'pipewire-audio', 'pipewire-pulse', 'wireplumber', 'alsa-utils', 'ttf-dejavu', 'noto-fonts',
    'konsole', 'firefox', 'plasma-integration', 'iwd', 'bluez', 'bluez-utils', 'brightnessctl',
    'xdg-user-dirs', 'noto-fonts-emoji', 'rtkit', 'udisks2', 'polkit', 'fprintd']
TOOL_ENV = {'PATH': '/usr/bin:/bin', 'HOME': '/root', 'LANG': 'C.UTF-8', 'LC_ALL': 'C.UTF-8'}
LOCALE_PATTERN = r'[A-Za-z][A-Za-z0-9_@.-]{0,63}'
KEYMAP_PATTERN = r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}'
ZONE_PATTERN = r'[A-Za-z0-9_+.-]+(?:/[A-Za-z0-9_+.-]+)*'
SDDM_THEME_FILES = {f'usr/share/sddm/themes/platinum-reality/{name}'
                    for name in ('Main.qml', 'metadata.desktop', 'theme.conf')}
LIVE_ROOT_EXCLUDES = (
    'boot', 'dev', 'proc', 'sys', 'run', 'tmp', 'home', 'root', 'var/tmp', 'var/log',
    'var/cache/pacman/pkg', 'etc/pacman.d/gnupg', 'etc/machine-id',
    'var/lib/dbus/machine-id', 'var/lib/systemd/random-seed', 'var/lib/systemd/network',
    'var/lib/iwd', 'var/lib/bluetooth', 'var/lib/cloud', 'var/lib/sddm',
    'opt/platinum-reality/repo', 'usr/bin/platinum-reality-install',
    'usr/lib/platinum-reality/installer-engine', 'usr/lib/platinum-reality/installer-boot')
LIVE_ONLY_FILES = (
    'etc/sddm.conf.d/10-platinum-live.conf', 'etc/sudoers.d/10-platinum-live',
    'etc/ssh/sshd_config.d/10-archiso.conf', 'etc/mkinitcpio.conf.d/archiso.conf',
    'etc/systemd/logind.conf.d/do-not-suspend.conf',
    'etc/systemd/journald.conf.d/volatile-storage.conf',
    'etc/systemd/resolved.conf.d/archiso.conf',
    'etc/systemd/system/systemd-networkd-wait-online.service.d/wait-for-only-one-interface.conf',
    'etc/systemd/network/20-ethernet.network', 'etc/systemd/network/20-wwan.network',
    'etc/systemd/system/getty@tty1.service.d/autologin.conf',
    'etc/systemd/system/getty@tty1.service.d/override.conf',
    'etc/systemd/system/serial-getty@ttyS0.service.d/autologin.conf',
    'etc/pacman.d/hooks/uncomment-mirrors.hook',
    'etc/pacman.d/hooks/zzzz99-remove-custom-hooks-from-airootfs.hook',
    'usr/local/lib/platinum-live-ready.py', 'usr/local/bin/choose-mirror',
    'usr/local/bin/Installation_guide', 'usr/local/bin/livecd-sound',
    'opt/platinum-reality/image.json', 'var/lib/pacman/db.lck', 'var/lib/alsa/asound.state')
LIVE_UNITS = frozenset(('platinum-live-ready.service', 'pacman-init.service',
    'etc-pacman.d-gnupg.mount', 'choose-mirror.service', 'livecd-alsa-unmuter.service',
    'livecd-talk.service', 'sshd.service', 'cloud-init.target', 'cloud-init.service',
    'cloud-init-local.service', 'cloud-init-network.service', 'cloud-config.service', 'cloud-final.service'))
LIVE_ONLY_PACKAGES = frozenset(('archinstall', 'mkinitcpio-archiso', 'cloud-init'))


class Refused(ValueError):
    pass


def unique_object(pairs):
    output = {}
    for key, value in pairs:
        if key in output:
            raise Refused('Duplicate JSON field')
        output[key] = value
    return output


def read_config(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    with os.fdopen(fd, 'rb') as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > 16384:
            raise Refused('Configuration must be a small regular file')
        raw = source.read(16385)
        if len(raw) > 16384:
            raise Refused('Configuration is too large')
    return json.loads(raw, object_pairs_hook=unique_object)


def validate_timezone(zone, zoneinfo=Path('/usr/share/zoneinfo')):
    if (not isinstance(zone, str) or not re.fullmatch(ZONE_PATTERN, zone) or
            any(part in ('.', '..') for part in zone.split('/')) or
            not (zoneinfo/zone).is_file() or not (zoneinfo/zone).resolve().is_relative_to(zoneinfo.resolve())):
        raise Refused('Choose an installed IANA timezone')
    with (zoneinfo/zone).open('rb') as source:
        if source.read(4) != b'TZif':
            raise Refused('Timezone must refer to compiled timezone data')


def validate_config(config, *, zoneinfo=Path('/usr/share/zoneinfo')):
    fields = {'version', 'disk', 'hostname', 'timezone', 'locale', 'keymap', 'user', 'encrypt', 'network'}
    if type(config) is not dict or set(config) != fields:
        raise Refused('Expected exactly the version 1 configuration fields; secrets go on stdin')
    if type(config['version']) is not int or config['version'] != 1:
        raise Refused('Unsupported configuration version')
    if not isinstance(config['disk'], str) or not re.fullmatch(r'/dev/[A-Za-z0-9_-]+', config['disk']):
        raise Refused('Choose the exact whole-device path returned by disks')
    host = config['hostname']
    if not isinstance(host, str) or not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', host):
        raise Refused('Hostname must be one lowercase DNS label')
    user = config['user']
    if (type(user) is not dict or set(user) != {'name', 'full_name'} or
            not isinstance(user['name'], str) or not re.fullmatch(r'[a-z_][a-z0-9_-]{0,30}', user['name']) or
            user['name'] in {'root', 'liveuser', 'sddm', 'nobody'}):
        raise Refused('Choose a new regular Linux account name')
    if (not isinstance(user['full_name'], str) or not 1 <= len(user['full_name']) <= 128 or
            any(ord(char) < 32 or char in ':\x7f' for char in user['full_name'])):
        raise Refused('Full name contains unsupported account-field characters')
    validate_timezone(config['timezone'], zoneinfo)
    for key, pattern in [('locale', LOCALE_PATTERN), ('keymap', KEYMAP_PATTERN)]:
        if not isinstance(config[key], str) or not re.fullmatch(pattern, config[key]):
            raise Refused('Invalid '+key)
    if type(config['encrypt']) is not bool or config['network'] != 'iwd-networkd':
        raise Refused('Expected encrypt boolean and iwd-networkd networking')
    return config


def require_live_vm():
    """'vm' in an owned QEMU/KVM guest, or 'physical' on a real computer booted from the live media.

    On a real computer internal disks can be installed on too (Lily Hewitt, 2026-11-10: the public ISO,
    and her own install onto potato's NVMe); USB-only was the rule for her first portable install (11-07).
    """
    if os.geteuid() != 0:
        raise Refused('Run the engine as root from the live media')
    virtual = subprocess.run(['systemd-detect-virt', '--vm'], capture_output=True, text=True, check=False, env=TOOL_ENV)
    kind = virtual.stdout.strip()
    if virtual.returncode == 0 and kind not in ('qemu', 'kvm'):
        raise Refused('Only QEMU/KVM virtual machines and real computers are supported; nothing changed')
    container = subprocess.run(['systemd-detect-virt', '--container'], capture_output=True, check=False, env=TOOL_ENV)
    if container.returncode == 0 or not BOOT_MOUNT.is_mount() or not Path('/etc/arch-release').is_file():
        raise Refused('Requires the Platinum Reality live media; nothing changed')
    return 'vm' if virtual.returncode == 0 else 'physical'


def installable_disks(disks, host):
    """Mark each disk installable or not, with the reason shown in the Installer."""
    for disk in disks:
        reason = ''
        if disk['is_boot_medium']:
            reason = 'the installation disk'
        disk['installable'] = not reason
        disk['unavailable'] = reason
    return disks


def read_json_command(argv):
    result = subprocess.run(argv, capture_output=True, text=True, check=True, env=TOOL_ENV)
    return json.loads(result.stdout)


def inventory_from(lsblk, boot_mount):
    mounts = boot_mount.get('filesystems', [])
    if len(mounts) != 1 or mounts[0].get('target') != str(BOOT_MOUNT):
        raise Refused('Cannot authenticate the actual live boot mount')
    boot_number = mounts[0].get('maj:min')
    nodes, parents = {}, {}

    def walk(node, parent=None):
        number = node.get('maj:min')
        if not isinstance(number, str) or not re.fullmatch(r'[0-9]+:[0-9]+', number):
            raise Refused('Incomplete block-device identity information')
        nodes[number] = node
        if parent:
            parents.setdefault(number, set()).add(parent)
        for child in node.get('children', []):
            walk(child, number)

    for node in lsblk.get('blockdevices', []):
        walk(node)
    if boot_number not in nodes:
        raise Refused('Live boot source is missing from the block-device graph')
    boot_ancestors, pending = set(), [boot_number]
    while pending:
        number = pending.pop()
        if number not in boot_ancestors:
            boot_ancestors.add(number)
            pending.extend(parents.get(number, ()))
    if not any(nodes[number].get('type') in ('disk', 'rom') for number in boot_ancestors):
        # Includes unsupported loop/remote/complex Ventoy arrangements. A false
        # refusal is preferable to marking the boot USB as an erasable disk.
        raise Refused('Cannot resolve live media to its physical disk; refusing all targets')
    output = []
    for number, node in nodes.items():
        if node.get('type') not in ('disk', 'rom'):
            continue
        descendants = []

        def descend(parent):
            for child in parent.get('children', []):
                descendants.append(child)
                descend(child)
        descend(node)
        output.append({'path': node['path'], 'model': (node.get('model') or '').strip(),
            'size_bytes': int(node['size']), 'removable': bool(node.get('rm')),
            'transport': node.get('tran') or '', 'is_boot_medium': number in boot_ancestors,
            'partitions': [{'path': child['path'], 'fstype': child.get('fstype') or '',
                'label': child.get('label') or '', 'size_bytes': int(child['size'])}
                for child in descendants if child.get('type') == 'part'],
            '_number': number, '_type': node['type'], '_readonly': bool(node.get('ro')),
            '_busy': any(x for child in [node, *descendants] for x in (child.get('mountpoints') or []))})
    return sorted(output, key=lambda item: item['path'])


def inventory():
    # PATH alone does not request JSON children[]. Explicit tree output is
    # needed both for partition warnings and rejecting mounted descendants.
    return inventory_from(read_json_command(['lsblk', '--json', '--tree', '--bytes', '--paths', '--output',
        'PATH,TYPE,MODEL,SIZE,RM,TRAN,FSTYPE,LABEL,MOUNTPOINTS,MAJ:MIN,RO']),
        read_json_command(['findmnt', '--json', '--mountpoint', str(BOOT_MOUNT), '--output', 'TARGET,MAJ:MIN']))


def public_disks(disks):
    return [{key: value for key, value in disk.items() if not key.startswith('_')} for disk in disks]


def select_disk(config, disks, host='vm'):
    found = [disk for disk in disks if disk['path'] == config['disk']]
    if len(found) != 1:
        raise Refused('Destination is not one whole device returned by disks')
    disk = found[0]
    if disk['is_boot_medium']:
        raise Refused('The live boot medium cannot be an installation destination')
    if disk['_type'] != 'disk' or disk['_readonly'] or disk['_busy']:
        raise Refused('Destination must be a writable, unmounted whole disk with no active swap')
    if disk['size_bytes'] < MIN_DISK_BYTES:
        raise Refused('The destination needs at least 16 GiB')
    return disk


def plan(config, disks, host='vm'):
    disk = select_disk(config, disks, host)
    partitions = [{key: partition[key] for key in ('path', 'fstype', 'label', 'size_bytes')}
                  for partition in disk['partitions']]
    # Shown in the Installer's Erase Disk question. Since the live-copy source, nothing here is an
    # "offline repository" any more; Secure Boot is still unsupported.
    warnings = ['This erases the ENTIRE destination disk.',
                'Secure Boot is not supported: turn it off in the computer\'s firmware settings first.']
    if partitions:
        warnings.append('The destination already contains '+str(len(partitions))+
                        ' partition(s); every listed partition will be removed.')
    return {'ok': True, 'erases': [disk['path']],
            'existing_partitions': partitions,
            'steps': [{'id': name, 'title': title} for name, title in STEPS],
            'warnings': warnings}


def read_secrets(stream, encrypted):
    raw = stream.read(16385)
    if len(raw) > 16384:
        raise Refused('Secret input is too large')
    values = json.loads(raw, object_pairs_hook=unique_object)
    expected = {'password', 'passphrase'} if encrypted else {'password'}
    if type(values) is not dict or set(values) != expected:
        raise Refused('Expected password and, for encryption, passphrase on stdin only')
    for value in values.values():
        if (not isinstance(value, str) or not 8 <= len(value) <= 1024 or
                any(char in value for char in '\x00\n\r')):
            raise Refused('Secrets must be 8–1024 characters with no NUL or newline')
    return values


class Progress:
    def __init__(self, fd):
        if fd < 0 or fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_ACCMODE == os.O_RDONLY:
            raise Refused('Progress descriptor must already be writable')
        self.fd, self.step = fd, 'validate'

    def send(self, value):
        payload = (json.dumps(value, ensure_ascii=True)+'\n').encode()
        while payload:
            payload = payload[os.write(self.fd, payload):]

    def update(self, step, fraction, message):
        self.step = step
        self.send({'step': step, 'fraction': fraction, 'message': message})


def tool(argv, *, input=None, capture=False, pass_fds=()):
    # Never put input (credentials) in diagnostics or exception text.
    extra = {'pass_fds': tuple(pass_fds)} if pass_fds else {}
    result = subprocess.run([str(value) for value in argv], input=input, check=False, timeout=1800,
        stdout=subprocess.PIPE if capture else sys.stderr, stderr=sys.stderr, env=TOOL_ENV, **extra)
    if result.returncode:
        raise Refused('Installation command failed: '+str(argv[0]))
    return result.stdout if capture else None


def reread_partition_table(disk, attempts=10, delay=0.5):
    # sgdisk has just replaced the table. udev is still probing the OLD partitions (on a disk left by an
    # interrupted install: an ESP and a LUKS partition), which holds them open and makes BLKRRPART fail
    # with EBUSY. A blank disk has nothing to probe. Let udev finish, then retry briefly; on each busy
    # attempt record what still holds the disk (stderr only, no credentials). Sammy, 2026-11-01.
    tool(['udevadm', 'settle'])
    for attempt in range(1, attempts + 1):
        try:
            tool(['blockdev', '--rereadpt', disk])
        except Refused:
            print('partition table busy (attempt', attempt, 'of', str(attempts) + '); holders:', file=sys.stderr)
            subprocess.run(['lsblk', '--paths', '--output', 'PATH,TYPE,FSTYPE,MOUNTPOINTS', str(disk)], check=False,
                           timeout=30, stdout=sys.stderr, stderr=sys.stderr, env=TOOL_ENV)
            tool(['udevadm', 'settle'])
            time.sleep(delay)
            continue
        if attempt > 1:
            print('partition table reread on attempt', attempt, file=sys.stderr)
        return attempt
    raise Refused('Installation command failed: blockdev (the partition table stayed busy); '
                  'the destination was already erased')


def validate_boot_assets(assets=BOOT_ASSETS, theme=BOOT_THEME, erased=False):
    # Consistency checks for the immutable image's exact native-pin helper and
    # original rendering, not a signature trust boundary for a hostile live root.
    # Checked before erasure (validate_repo) and again after it (install_boot_theme):
    # the message must say which, so a refusal never claims an erased disk is intact.
    outcome = 'the destination was already erased' if erased else 'nothing erased'
    hashes = json.loads((assets/'sha256.json').read_text())
    if type(hashes) is not dict or set(hashes) != BOOT_ASSET_FILES:
        raise Refused('Live boot helper inventory is incomplete; '+outcome)
    for name, expected in hashes.items():
        path = assets/name
        if (not path.is_file() or path.is_symlink() or path.stat().st_size > 1024**2 or
                not path.resolve().is_relative_to(assets.resolve())):
            raise Refused('Invalid live boot helper path; '+outcome)
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise Refused('Live boot helper checksum mismatch; '+outcome)
    for name in ('manifest.json', 'platinum-reality.script', 'platinum-reality.plymouth', 'splash.bmp'):
        path = theme/name
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(theme.resolve()):
            raise Refused('Live boot rendering is incomplete; '+outcome)
    if json.loads((theme/'manifest.json').read_text()).get('icon') != 'original':
        raise Refused('Installer boot artwork must be the original project rendering; '+outcome)
    if '@' in (theme/'platinum-reality.script').read_text():
        raise Refused('Unfilled boot-theme parameter; '+outcome)
    # Import only the hash-checked helper from the immutable image, not an
    # installed application/user module. Its checker performs no commands.
    spec = importlib.util.spec_from_file_location('image_boot_splash', assets/'scripts/boot-splash.py')
    helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
    try:
        helper.checked_rendering(theme)
    except (SystemExit, ValueError, KeyError, OSError) as error:
        raise Refused('Incomplete or invalid live boot rendering; '+outcome) from error
    return helper


def validate_repo(repo=REPO):
    record = json.loads((repo/'provenance.json').read_text())
    names = set()
    theme_packages = 0
    files = record.get('packages')
    if not isinstance(files, list) or not files:
        raise Refused('Offline package inventory is absent')
    for item in files:
        name = item['file']
        if not re.fullmatch(r'[A-Za-z0-9@+_.:-]+\.pkg\.tar\.zst', name):
            raise Refused('Invalid offline package filename')
        path = repo/name
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        with os.fdopen(fd, 'rb') as source:
            info = os.fstat(source.fileno())
            if (not stat.S_ISREG(info.st_mode) or info.st_size != item['size_bytes'] or
                    hashlib.file_digest(source, 'sha256').hexdigest() != item['sha256']):
                raise Refused('Offline package checksum mismatch')
        metadata = tool(['bsdtar', '-xOf', path, '.PKGINFO'], capture=True).decode()
        package_names = {line.removeprefix('pkgname = ') for line in metadata.splitlines()
                         if line.startswith('pkgname = ')}
        names.update(package_names)
        if 'platinum-reality' in package_names:
            payload = {line.removeprefix('./') for line in
                       tool(['bsdtar', '-tf', path], capture=True).decode().splitlines()}
            if not SDDM_THEME_FILES.issubset(payload):
                raise Refused('Offline desktop package lacks the Platinum SDDM theme; nothing erased')
            theme_packages += 1
    if not set(TARGET_PACKAGES).issubset(names):
        raise Refused('Offline repository lacks required installation packages')
    if 'networkmanager' in names:
        raise Refused('NetworkManager is outside the installation contract')
    if theme_packages != 1:
        raise Refused('Expected exactly one offline Platinum desktop package')
    validate_boot_assets()


def live_root_checksum(raw):
    """Archiso's detached checksum authenticates bytes, not a publisher."""
    if not isinstance(raw, bytes) or len(raw) > 256:
        raise Refused('Invalid immutable live-root checksum record')
    try:
        record = raw.decode('ascii')
    except UnicodeDecodeError as error:
        raise Refused('Invalid immutable live-root checksum encoding') from error
    match = re.fullmatch(r'([0-9a-f]{128})[ \t]+\*?airootfs\.sfs\n?', record)
    if not match:
        raise Refused('Expected exactly the live-root checksum and filename')
    return match[1]


def live_account_records(records):
    """Validate immutable account data before erasure; remove the live identity.

    This pure transform never invokes account tools against live PIDs, prints
    shadow contents, or accepts a caller-chosen username to delete.
    """
    columns = {'passwd': 7, 'shadow': 9, 'group': 4, 'gshadow': 4}
    if set(records) != set(columns):
        raise Refused('Incomplete immutable account databases')
    tables = {}
    for name, count in columns.items():
        raw = records[name]
        if not isinstance(raw, str) or len(raw.encode('utf-8')) > 128 * 1024 or '\0' in raw:
            raise Refused('Invalid immutable account database')
        rows = [line.split(':') for line in raw.splitlines() if line]
        if (not rows or any(len(row) != count or not row[0] for row in rows) or
                len({row[0] for row in rows}) != len(rows)):
            raise Refused('Malformed or duplicate immutable account records')
        tables[name] = rows
    users = {row[0]: row for row in tables['passwd']}
    root, live = users.get('root'), users.get('liveuser')
    if (not root or root[2:4] != ['0', '0'] or not live or
            live[2:4] != ['1000', '1000'] or live[5] != '/home/liveuser'):
        raise Refused('Unexpected root/live account identities')
    for row in users.values():
        if not all(re.fullmatch(r'[0-9]+', row[i]) for i in (2, 3)):
            raise Refused('Invalid immutable account IDs')
        uid = int(row[2])
        if ((uid == 0 and row[0] != 'root') or
                (uid >= 1000 and row[0] not in ('liveuser', 'nobody')) or
                (row[0] == 'nobody' and uid != 65534)):
            raise Refused('Unexpected regular or privileged account in immutable image')
    shadows = {row[0]: row for row in tables['shadow']}
    if not set(users).issubset(shadows):
        raise Refused('Immutable account/shadow identities differ')
    for row in tables['shadow']:
        if row[0] != 'liveuser' and (not row[1] or row[1][0] not in ('!', '*')):
            raise Refused('Reusable password or unlocked account in immutable image')
    groups = {row[0]: row for row in tables['group']}
    if (groups.get('root', [None] * 4)[2] != '0' or
            groups.get('liveuser', [None] * 4)[2] != '1000'):
        raise Refused('Unexpected root/live group identities')
    for row in tables['group']:
        if not re.fullmatch(r'[0-9]+', row[2]):
            raise Refused('Invalid immutable group ID')
    output = {}
    for name, rows in tables.items():
        kept = []
        for row in rows:
            if row[0] == 'liveuser':
                continue
            row = row.copy()
            for column in ((3,) if name == 'group' else (2, 3) if name == 'gshadow' else ()):
                row[column] = ','.join(user for user in row[column].split(',')
                                       if user and user != 'liveuser')
            kept.append(':'.join(row))
        output[name] = '\n'.join(kept) + '\n'
    return output


def installed_pacman_config(raw):
    """Keep normal mirrors; discard only the live project's local repo stanza."""
    if not isinstance(raw, str) or len(raw.encode('utf-8')) > 128 * 1024 or '\0' in raw:
        raise Refused('Invalid immutable package-manager configuration')
    kept, skipping, occurrences = [], False, 0
    for line in raw.splitlines(keepends=True):
        meaningful = line.split('#', 1)[0].strip()
        section = re.fullmatch(r'\[([^\]]+)\]', meaningful)
        if section:
            skipping = section[1] == 'platinum-reality'
            occurrences += skipping
        if not skipping:
            if 'file://' in meaningful:
                raise Refused('Unexpected local package repository in immutable image')
            kept.append(line)
    if occurrences > 1:
        raise Refused('Duplicate live repository configuration')
    result = ''.join(kept)
    if not re.search(r'(?m)^\s*\[core\]\s*(?:#.*)?$', result):
        raise Refused('Installed Arch repository configuration is absent')
    return result


def bounded_regular(path, limit):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise Refused('Expected a bounded regular installation-source record')
        raw = stream.read(limit + 1)
        if len(raw) > limit:
            raise Refused('Installation-source record grew during reading')
        return raw


def image_policy(raw):
    try:
        policy = json.loads(raw, object_pairs_hook=unique_object)
    except (ValueError, UnicodeDecodeError) as error:
        raise Refused('Invalid immutable image policy') from error
    if type(policy) is not dict or type(policy.get('version')) is not int:
        raise Refused('Missing immutable image policy version')
    return policy


def installation_source_kind(path=None):
    try:
        policy = image_policy(bounded_regular(path or IMAGE_RECORD, 16384))
    except FileNotFoundError:
        return 'offline-repository'  # Retains the original fixture/legacy API.
    if policy['version'] == 1 and 'installation_source' not in policy:
        return 'offline-repository'
    if policy['version'] == 2 and policy.get('installation_source') == 'live-squashfs-v1':
        return 'live-squashfs'
    raise Refused('Unsupported installation-source policy; no fallback')


def live_root_inventory(listing):
    """Read numeric unsquashfs metadata, keeping regular-file names and sizes."""
    if len(listing) > 32 * 1024**2:
        raise Refused('Unexpectedly large immutable filesystem inventory')
    files = {}
    for line in listing.decode('utf-8').splitlines():
        if not line.startswith('-'):
            continue
        fields = line.split(maxsplit=5)
        if (len(fields) != 6 or not fields[2].isdigit() or
                not fields[5].startswith('squashfs-root/')):
            raise Refused('Unrecognized immutable filesystem inventory')
        name = fields[5].removeprefix('squashfs-root/')
        if (name in files or name.startswith('/') or
                any(part in ('', '.', '..') for part in name.split('/'))):
            raise Refused('Invalid or duplicate immutable filesystem path')
        files[name] = int(fields[2])
    if not files:
        raise Refused('Immutable filesystem inventory has no regular files')
    return files


def live_path_excluded(name):
    return any(name == path or name.startswith(path + '/') for path in LIVE_ROOT_EXCLUDES)


@contextmanager
def authenticated_live_root(boot=None, engine_path=None):
    """Keep the authenticated immutable source FD alive through installation."""
    boot = boot or BOOT_MOUNT
    for name in ('arch', 'arch/x86_64'):
        parent = boot/name
        if parent.is_symlink() or not parent.is_dir():
            raise Refused('Unsupported live-root source path')
    directory = boot/'arch/x86_64'
    expected = live_root_checksum(bounded_regular(directory/'airootfs.sha512', 256))
    fd = os.open(directory/'airootfs.sfs', os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    with os.fdopen(fd, 'rb') as image:
        info = os.fstat(image.fileno())
        if (not stat.S_ISREG(info.st_mode) or not 4096 <= info.st_size <= 8 * 1024**3 or
                image.read(4) != b'hsqs'):
            raise Refused('Unsupported immutable live-root file')
        image.seek(0)
        if hashlib.file_digest(image, 'sha512').hexdigest() != expected:
            raise Refused('Immutable live-root checksum mismatch; nothing erased')
        image.seek(0)
        descriptor = image.fileno()
        name = '/proc/self/fd/' + str(descriptor)

        def read(path, limit=128 * 1024):
            raw = tool(['unsquashfs', '-cat', name, path], capture=True, pass_fds=(descriptor,))
            if len(raw) > limit:
                raise Refused('Unexpectedly large immutable source metadata')
            return raw

        policy = image_policy(read('opt/platinum-reality/image.json', 16384))
        pin = policy.get('native_source', {})
        if (policy['version'] != 2 or policy.get('installation_source') != 'live-squashfs-v1' or
                type(pin) is not dict or
                any(not isinstance(pin.get(k), str) or not re.fullmatch(r'[0-9a-f]{40}', pin[k])
                    for k in ('commit', 'distribution_commit'))):
            raise Refused('Live root does not declare the expected immutable source policy')
        engine_bytes = read('usr/lib/platinum-reality/installer-engine', 1024**2)
        running_engine = bounded_regular(engine_path or Path(__file__), 1024**2)
        if (hashlib.sha256(engine_bytes).hexdigest() != pin.get('engine_sha256') or
                engine_bytes != running_engine):
            raise Refused('Running engine differs from the immutable installation source')
        wrapper = read('usr/bin/platinum-reality-install')
        if hashlib.sha256(wrapper).hexdigest() != pin.get('wrapper_sha256'):
            raise Refused('Immutable installer wrapper provenance differs')
        accounts = {key: read('etc/' + key).decode('utf-8')
                    for key in ('passwd', 'shadow', 'group', 'gshadow')}
        sanitized_accounts = live_account_records(accounts)
        pacman = installed_pacman_config(read('etc/pacman.conf').decode('utf-8'))
        listing = tool(['unsquashfs', '-lln', name], capture=True, pass_fds=(descriptor,))
        files = live_root_inventory(listing)
        if not SDDM_THEME_FILES.issubset(files.keys()):
            raise Refused('Immutable live root lacks the target login theme')
        paths = sorted(path for path in files if re.fullmatch(r'var/lib/pacman/local/[^/]+/desc', path))
        if not paths or len(paths) > 4096:
            raise Refused('Immutable installed package database is missing or unbounded')
        descriptions = tool(['unsquashfs', '-cat', name, *paths], capture=True, pass_fds=(descriptor,))
        if len(descriptions) > 16 * 1024**2:
            raise Refused('Immutable installed package records are unbounded')
        packages = re.findall(rb'(?m)^%NAME%\n([^\n]+)', descriptions)
        if len(packages) != len(paths) or len(set(packages)) != len(packages):
            raise Refused('Immutable installed package identities differ from their inventory')
        names = {package.decode('ascii') for package in packages}
        if not set(TARGET_PACKAGES).issubset(names) or 'networkmanager' in names:
            raise Refused('Immutable live root lacks required target packages or has an unsupported network stack')
        validate_boot_assets()
        yield {'fd': descriptor, 'identity': (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns),
               'accounts_original': accounts, 'accounts': sanitized_accounts,
               'pacman_config': pacman, 'policy': policy, 'packages': names,
               'files': {path: size for path, size in files.items() if not live_path_excluded(path)}}


def live_target_path(root, name):
    """Fixed target-relative paths only; never follow ancestors into the live OS."""
    if (not isinstance(name, str) or name.startswith('/') or
            any(part in ('', '.', '..') for part in name.split('/')) or
            root.is_symlink() or not root.is_dir() or root.resolve() == Path('/')):
        raise Refused('Invalid new-target path')
    parent = root
    for part in name.split('/')[:-1]:
        parent = parent/part
        if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
            raise Refused('Unexpected symlink or object in target path')
    return root/name


def unlink_live_target_file(root, name):
    path = live_target_path(root, name)
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if not (stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)):
        raise Refused('Unexpected object in live-only target cleanup')
    path.unlink()  # A leaf symlink itself, never its destination.


def sanitize_live_target(root, source):
    # Check copied account bytes before changing them. No source/overlay paths
    # are written, and a bad exclusion cannot silently retain a live home.
    for name in ('passwd', 'shadow', 'group', 'gshadow'):
        path = live_target_path(root, 'etc/' + name)
        if bounded_regular(path, 128 * 1024).decode('utf-8') != source['accounts_original'][name]:
            raise Refused('Copied account data differs from its immutable preflight')
    for name, mode in (('home', 0o755), ('root', 0o700), ('dev', 0o755), ('proc', 0o755),
                       ('sys', 0o755), ('run', 0o755), ('tmp', 0o1777),
                       ('var/tmp', 0o1777), ('var/log', 0o755)):
        path = live_target_path(root, name)
        if path.is_symlink() or (path.exists() and (not path.is_dir() or any(path.iterdir()))):
            raise Refused('Live home/runtime exclusion did not leave an empty target directory')
        path.mkdir(parents=True, exist_ok=True, mode=mode)
        os.chmod(path, mode)
    sudoers = live_target_path(root, 'etc/sudoers')
    directory = live_target_path(root, 'etc/sudoers.d')
    if directory.is_symlink():
        raise Refused('Unexpected target sudo-policy directory link')
    policies = [sudoers] + (list(directory.iterdir()) if directory.is_dir() else [])
    for path in policies:
        if not path.exists() and not path.is_symlink():
            continue
        if path == root/'etc/sudoers.d/10-platinum-live':
            continue
        content = bounded_regular(path, 128 * 1024).decode('utf-8')
        if any(re.search(r'\bNOPASSWD\s*:', line.split('#', 1)[0]) for line in content.splitlines()):
            raise Refused('Unexpected inherited passwordless sudo policy')
    for name in LIVE_ONLY_FILES:
        unlink_live_target_file(root, name)
    units = live_target_path(root, 'etc/systemd/system')
    if units.is_symlink():
        raise Refused('Unexpected system-service directory link')
    if units.is_dir():
        for path in list(units.rglob('*')):
            if path.name in LIVE_UNITS:
                unlink_live_target_file(root, path.relative_to(root).as_posix())
    ssh = live_target_path(root, 'etc/ssh')
    if ssh.is_symlink():
        raise Refused('Unexpected SSH identity directory link')
    if ssh.is_dir():
        for path in ssh.glob('ssh_host_*'):
            unlink_live_target_file(root, path.relative_to(root).as_posix())
    for name, data in source['accounts'].items():
        live_target_path(root, 'etc/' + name)
        write_target(root, 'etc/' + name, data, 0o600 if name in ('shadow', 'gshadow') else 0o644)
    for name in ('subuid', 'subgid'):
        unlink_live_target_file(root, 'etc/' + name)
        write_target(root, 'etc/' + name, '')
    for name in ('etc/machine-id', 'var/lib/dbus/machine-id', 'var/lib/systemd/random-seed'):
        unlink_live_target_file(root, name)
    write_target(root, 'etc/machine-id', '', 0o444)
    dbus = live_target_path(root, 'var/lib/dbus/machine-id')
    dbus.parent.mkdir(parents=True, exist_ok=True)
    dbus.symlink_to('../../../etc/machine-id')
    live_target_path(root, 'etc/pacman.conf')
    write_target(root, 'etc/pacman.conf', source['pacman_config'])
    live_target_path(root, 'etc/crypttab')
    write_target(root, 'etc/crypttab', '# Root unlock is described by the target-specific UKI.\n')
    write_target(root, 'etc/motd', 'Platinum Reality\n')


def copy_live_root(root, source):
    descriptor = source['fd']
    info = os.fstat(descriptor)
    if (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) != source['identity']:
        raise Refused('Immutable source identity changed after preflight')
    # Unlike mksquashfs -e, unsquashfs needs -excludes BEFORE the archive;
    # names after it then denote exclusions, not a selected subset to extract.
    tool(['unsquashfs', '-force', '-no-progress', '-processors', '4', '-d', root,
          '-excludes', '/proc/self/fd/' + str(descriptor), *LIVE_ROOT_EXCLUDES], pass_fds=(descriptor,))
    after = os.fstat(descriptor)
    if (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) != source['identity']:
        raise Refused('Immutable source changed during extraction')
    # A valid tool exit alone does not prove that it extracted the whole root.
    # In particular, a mistaken selection option can produce a tiny valid tree.
    for name, size in source['files'].items():
        path = live_target_path(root, name)
        try:
            info = path.lstat()
        except FileNotFoundError as error:
            raise Refused('Immutable root extraction is incomplete') from error
        if not stat.S_ISREG(info.st_mode) or info.st_size != size:
            raise Refused('Immutable root extraction differs from its inventory')
    for name in LIVE_ROOT_EXCLUDES:
        path = live_target_path(root, name)
        # The ESP is deliberately mounted before copying. Nothing else excluded
        # may have been extracted, including empty directory entries or links.
        if name != 'boot' and (path.exists() or path.is_symlink()):
            raise Refused('Live-only source exclusion failed')
    sanitize_live_target(root, source)
    # All privileged commands operate on the fresh target. Do not copy live
    # machine identity or a live signing key into the installed system.
    tool(['systemd-machine-id-setup', '--root', root])
    tool(['arch-chroot', root, 'pacman-key', '--init'])
    tool(['arch-chroot', root, 'pacman-key', '--populate', 'archlinux'])


def locale_definitions(locale_gen=Path('/etc/locale.gen'), supported=Path('/usr/share/i18n/SUPPORTED')):
    # Match actual glibc records, not two-word prose comments in locale.gen.
    supported_pairs = {tuple(line.split()) for line in supported.read_text().splitlines()
                       if len(line.split()) == 2}
    definitions = {}
    for line in locale_gen.read_text().splitlines():
        parts = line.lstrip().removeprefix('#').strip().split('#', 1)[0].split()
        if len(parts) != 2:
            continue
        locale, encoding = parts
        if ((locale, encoding) in supported_pairs and re.fullmatch(LOCALE_PATTERN, locale) and
                re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.:-]{0,63}', encoding)):
            previous = definitions.get(locale)
            if previous is not None and previous != encoding:
                raise Refused('Ambiguous live locale encoding')
            definitions[locale] = encoding
    return definitions


def installed_keymaps(keymaps=Path('/usr/share/kbd/keymaps')):
    names = set()
    root = keymaps.resolve()
    for path in keymaps.rglob('*'):
        suffix = next((ending for ending in ('.map.gz', '.map') if path.name.endswith(ending)), None)
        if not suffix:
            continue
        name = path.name.removesuffix(suffix)
        try:
            if (re.fullmatch(KEYMAP_PATTERN, name) and path.is_file() and
                    path.resolve().is_relative_to(root)):
                names.add(name)
        except (OSError, RuntimeError):
            continue # Broken or looping aliases are not installable choices.
    return sorted(names)


def validate_locale_and_keymap(config, *, locale_gen=Path('/etc/locale.gen'),
                               supported=Path('/usr/share/i18n/SUPPORTED'),
                               keymaps=Path('/usr/share/kbd/keymaps')):
    if config['locale'] != 'C.UTF-8' and config['locale'] not in locale_definitions(locale_gen, supported):
        raise Refused('Locale is not supported by the live installation tools')
    if config['keymap'] not in installed_keymaps(keymaps):
        raise Refused('Console keymap is not available; refusing an unusable encrypted boot')


def choices(*, locale_gen=Path('/etc/locale.gen'), supported=Path('/usr/share/i18n/SUPPORTED'),
            keymaps=Path('/usr/share/kbd/keymaps'), zoneinfo=Path('/usr/share/zoneinfo')):
    zones = []
    for path in zoneinfo.rglob('*'):
        name = path.relative_to(zoneinfo).as_posix()
        try:
            validate_timezone(name, zoneinfo)
        except (Refused, OSError, RuntimeError):
            continue
        zones.append(name)
    return {'locales': sorted(set(locale_definitions(locale_gen, supported)) | {'C.UTF-8'}),
            'timezones': sorted(set(zones)), 'keymaps': installed_keymaps(keymaps)}


def checked_device(config, expected):
    current = select_disk(config, inventory())
    info = os.stat(config['disk'], follow_symlinks=False)
    if (not stat.S_ISBLK(info.st_mode) or
            f'{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}' != current['_number'] or
            any(current[key] != expected[key] for key in ('_number', 'size_bytes', 'path'))):
        raise Refused('Destination identity changed; nothing further will be written')


def write_target(root, name, data, mode=0o644):
    # Only fixed engine-owned relative names are passed here, under a NEW ext4
    # filesystem. Never accept a path or file contents from caller configuration.
    path = root/name
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW | os.O_CLOEXEC, mode)
    with os.fdopen(fd, 'w') as output:
        output.write(data)
        os.fchmod(output.fileno(), mode)


def prepare_welcome(root, username):
    accounts = [line.split(':') for line in (root/'etc/passwd').read_text().splitlines()]
    account = next((entry for entry in accounts if entry[0] == username), None)
    if (not account or len(account) != 7 or account[5] != '/home/'+username or
            not account[2].isdigit() or not account[3].isdigit() or
            int(account[2]) == 0 or int(account[3]) == 0):
        raise Refused('New account has unexpected identity or home directory')
    home = root/'home'/username
    if not home.is_dir() or home.is_symlink() or not home.resolve().is_relative_to(root.resolve()):
        raise Refused('New account home is absent or invalid')
    for name in ('.config', '.config/platinum-reality'):
        directory = home/name
        directory.mkdir(exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(home.resolve()):
            raise Refused('New account configuration directory is invalid')
        directory.chmod(0o755)
        os.chown(directory, int(account[2]), int(account[3]))
    marker = home/'.config/platinum-reality/welcome-pending'
    write_target(root, str(marker.relative_to(root)), '', 0o644)
    os.chown(marker, int(account[2]), int(account[3]))


def install_boot_theme(root):
    helper = validate_boot_assets(erased=True)
    # --no-rebuild prevents all live pacman/theme/mkinitcpio calls. This helper
    # writes only under the new target; its rendering came from the pinned image.
    tool(['python3', '-I', BOOT_ASSETS/'scripts/boot-splash.py', 'install', '--root', root,
          '--from', BOOT_THEME, '--no-rebuild'])
    # Complete the frozen helper's omitted last rendered asset (bullet-8.png).
    theme = root/'usr/share/plymouth/themes/platinum-reality'
    for name in helper.RENDERED:
        shutil.copyfile(BOOT_THEME/name, theme/name)
    tool(['arch-chroot', root, 'plymouth-set-default-theme', 'platinum-reality'])


def configure_target(root, config, credentials, root_uuid, esp_uuid, luks_uuid, portable=False):
    def chroot(*args, **kwargs):
        return tool(['arch-chroot', root, *args], **kwargs)
    for name in SDDM_THEME_FILES:
        item = root/name
        if not item.is_file() or item.is_symlink() or not item.resolve().is_relative_to(root.resolve()):
            raise Refused('Installed Platinum SDDM theme is absent or invalid')
    write_target(root, 'etc/sddm.conf.d/10-platinum-reality.conf', '[Theme]\nCurrent=platinum-reality\n')
    write_target(root, 'etc/hostname', config['hostname']+'\n')
    write_target(root, 'etc/hosts', '127.0.0.1 localhost\n::1 localhost\n127.0.1.1 '+config['hostname']+'\n')
    target_zone = root/'usr/share/zoneinfo'/config['timezone']
    if not target_zone.is_file():
        raise Refused('Target timezone data is absent')
    (root/'etc/localtime').unlink(missing_ok=True)
    (root/'etc/localtime').symlink_to('../usr/share/zoneinfo/'+config['timezone'])
    if config['locale'] != 'C.UTF-8':
        encoding = locale_definitions().get(config['locale'])
        if not encoding:
            raise Refused('Locale is not supported by the live installation tools')
        write_target(root, 'etc/locale.gen', config['locale']+' '+encoding+'\n')
        chroot('locale-gen')
    write_target(root, 'etc/locale.conf', 'LANG='+config['locale']+'\n')
    write_target(root, 'etc/vconsole.conf', 'KEYMAP='+config['keymap']+'\n')
    chroot('useradd', '--create-home', '--groups', 'wheel', '--shell', '/bin/bash',
           '--comment', config['user']['full_name'], config['user']['name'])
    prepare_welcome(root, config['user']['name'])
    # chpasswd uses PAM to hash the password; plaintext is only on this pipe.
    chroot('chpasswd', input=(config['user']['name']+':'+credentials['password']+'\n').encode())
    chroot('passwd', '--lock', 'root')
    write_target(root, 'etc/sudoers.d/20-platinum-wheel', '%wheel ALL=(ALL:ALL) ALL\n', 0o440)
    write_target(root, 'etc/iwd/main.conf', '[General]\nEnableNetworkConfiguration=true\n\n[Network]\nNameResolvingService=systemd\n')
    write_target(root, 'etc/systemd/network/20-platinum-reality-wired.network', '[Match]\nName=en* eth*\n\n[Network]\nDHCP=yes\n')
    (root/'etc/resolv.conf').unlink(missing_ok=True)
    (root/'etc/resolv.conf').symlink_to('../run/systemd/resolve/stub-resolv.conf')
    # No live autologin, credentials, sudo policy or displays.json is copied.
    tool(['systemctl', '--root', root, 'enable', 'sddm.service', 'bluetooth.service', 'iwd.service',
          'systemd-networkd.service', 'systemd-resolved.service'])
    tool(['systemctl', '--root', root, '--global', 'enable', 'pipewire.socket', 'pipewire-pulse.socket', 'wireplumber.service'])
    tool(['systemctl', '--root', root, 'set-default', 'graphical.target'])
    chroot('systemd-tmpfiles', '--create', '/usr/lib/tmpfiles.d/sddm.conf')
    accounts = [line.split(':') for line in (root/'etc/passwd').read_text().splitlines()]
    sddm = next((entry for entry in accounts if entry[0] == 'sddm'), None)
    if not sddm or sddm[5] != '/var/lib/sddm':
        raise Refused('Unexpected SDDM state directory')
    parser = configparser.ConfigParser(interpolation=None); parser.optionxform = str
    parser['Last'] = {'Session': '/usr/share/wayland-sessions/platinum-reality.desktop'}
    contents = io.StringIO(); parser.write(contents, space_around_delimiters=False)
    write_target(root, 'var/lib/sddm/state.conf', contents.getvalue(), 0o600)
    os.chown(root/'var/lib/sddm/state.conf', int(sddm[2]), int(sddm[3]))
    chroot('runuser', '--user', config['user']['name'], '--', 'env',
           'HOME=/home/'+config['user']['name'], 'xdg-user-dirs-update')
    write_target(root, 'etc/fstab', 'UUID='+root_uuid+' / ext4 defaults 0 1\nUUID='+esp_uuid+' /boot vfat umask=0077 0 2\n')
    command_line = ('rd.luks.name='+luks_uuid+'=cryptroot root=/dev/mapper/cryptroot rw' if luks_uuid
                    else 'root=UUID='+root_uuid+' rw')
    # Explicit cmdline prevents inheriting live archiso parameters into the UKI.
    write_target(root, 'etc/kernel/cmdline', command_line+'\n')
    # A USB drive can be started on other computers: without autodetect the initramfs carries every
    # storage/USB/graphics driver, not only those of the computer it was installed from.
    hooks = 'base systemd' + ('' if portable else ' autodetect') + ' microcode modconf kms keyboard sd-vconsole block'
    if luks_uuid:
        hooks += ' sd-encrypt'
    hooks += ' filesystems fsck'
    # Sort BEFORE boot-splash.py's platinum-reality-plymouth.conf; otherwise
    # mkinitcpio would override the inserted Plymouth hook afterwards.
    write_target(root, 'etc/mkinitcpio.conf.d/10-platinum-reality.conf', 'HOOKS=('+hooks+')\n')
    write_target(root, 'etc/mkinitcpio.d/linux.preset',
        "# Keep the default config search: -c would disable our conf.d hooks.\nALL_kver=\"/boot/vmlinuz-linux\"\n"
        "PRESETS=('default')\ndefault_uki=\"/boot/EFI/Linux/arch-linux.efi\"\n"
        "default_options=\"--cmdline /etc/kernel/cmdline\"\n")
    kernels = [p.parent/'vmlinuz' for p in (root/'usr/lib/modules').glob('*/pkgbase')
               if p.read_text().strip() == 'linux' and (p.parent/'vmlinuz').is_file()]
    if len(kernels) != 1:
        raise Refused('Cannot identify the installed kernel')
    shutil.copyfile(kernels[0], root/'boot/vmlinuz-linux')
    (root/'boot/EFI/Linux').mkdir(parents=True, exist_ok=True)
    install_boot_theme(root)


def perform_install(config, expected, credentials, progress, *, source=None):
    # Temporary paths contain NO password/passphrase. All mounts/mappers are
    # invocation-owned and cleaned in reverse order, including on exceptions.
    mapper = None
    mounted_root = mounted_esp = False
    # Do not use TemporaryDirectory: its automatic rmtree could traverse a
    # still-mounted installed filesystem when unmounting fails.
    workspace = tempfile.mkdtemp(prefix='platinum-install-', dir='/run')
    try:
        work = Path(workspace); root = work/'target'; root.mkdir()
        try:
            checked_device(config, expected)
            progress.update('partition', .1, 'Erasing the confirmed VM destination')
            tool(['sgdisk', '--zap-all', config['disk']])
            tool(['sgdisk', '--new=1:0:+1G', '--typecode=1:ef00', '--change-name=1:EFI System',
                  '--new=2:0:0', '--typecode=2:'+('8309' if config['encrypt'] else '8300'),
                  '--change-name=2:Hard Disk', config['disk']])
            reread_partition_table(config['disk']); tool(['udevadm', 'settle'])
            rows = tool(['lsblk', '--noheadings', '--raw', '--paths', '--output', 'PATH,PARTN', config['disk']], capture=True).decode().splitlines()
            parts = {int(values[1]): values[0] for row in rows if len(values := row.split()) == 2 and values[1].isdigit()}
            if set(parts) != {1, 2}:
                raise Refused('New partition devices did not appear')
            progress.update('format', .2, 'Creating the ESP and root filesystem')
            tool(['mkfs.fat', '-F', '32', '-n', 'PLATINUMEFI', parts[1]])
            root_device, luks_uuid = parts[2], None
            if config['encrypt']:
                tool(['cryptsetup', 'luksFormat', '--type', 'luks2', '--batch-mode', '--key-file', '-', parts[2]],
                     input=credentials['passphrase'].encode())
                candidate = 'pr-install-'+secrets.token_hex(8)
                tool(['cryptsetup', 'open', '--key-file', '-', parts[2], candidate], input=credentials['passphrase'].encode())
                mapper = candidate
                root_device = '/dev/mapper/'+mapper
                luks_uuid = tool(['cryptsetup', 'luksUUID', parts[2]], capture=True).decode().strip()
            tool(['mkfs.ext4', '-F', '-L', 'Hard Disk', root_device])
            root_uuid = tool(['blkid', '-s', 'UUID', '-o', 'value', root_device], capture=True).decode().strip()
            esp_uuid = tool(['blkid', '-s', 'UUID', '-o', 'value', parts[1]], capture=True).decode().strip()
            progress.update('mount', .3, 'Mounting the new destination')
            tool(['mount', root_device, root]); mounted_root = True
            (root/'boot').mkdir(); tool(['mount', parts[1], root/'boot']); mounted_esp = True
            if source is None:
                pacman = work/'pacman.conf'
                pacman.write_text('[options]\nArchitecture=auto\nSigLevel=Required DatabaseOptional\n\n'
                                  '[platinum-reality]\nSigLevel=Optional TrustAll\nServer=file://'+str(REPO)+'\n')
                progress.update('packages', .4, 'Installing exclusively from the offline repository')
                tool(['pacstrap', '-K', '-C', pacman, root, *TARGET_PACKAGES])
            else:
                progress.update('packages', .4, 'Installing from the checked immutable live image')
                copy_live_root(root, source)
            progress.update('configure', .7, 'Creating the account and system settings')
            configure_target(root, config, credentials, root_uuid, esp_uuid, luks_uuid,
                             portable=expected.get('transport') == 'usb')
            if source is not None:
                remove = sorted(source['packages'] & LIVE_ONLY_PACKAGES)
                if remove:
                    # Run hooks only after the target's kernel/configuration is
                    # ready; never ask a package script to stop live PID 1 units.
                    tool(['arch-chroot', root, 'env', 'SYSTEMD_OFFLINE=1', 'pacman',
                          '-Rns', '--noconfirm', *remove])
            progress.update('bootloader', .85, 'Building the installed UKI and systemd-boot')
            tool(['arch-chroot', root, 'mkinitcpio', '-P'])
            tool(['arch-chroot', root, 'bootctl', '--esp-path=/boot', '--no-variables', 'install'])
            write_target(root, 'boot/loader/loader.conf', 'default arch-linux.efi\ntimeout 3\nconsole-mode auto\n')
            if not (root/'boot/EFI/Linux/arch-linux.efi').is_file() or not (root/'boot/EFI/BOOT/BOOTX64.EFI').is_file():
                raise Refused('Installed UKI or fallback bootloader is absent')
            progress.update('finish', .95, 'Flushing and unmounting the destination')
            tool(['sync'])
        finally:
            # Do not silently report success if cleanup fails; do not recursively
            # remove a temporary directory while its target is still mounted.
            cleanup_errors = []
            keyring = root/'etc/pacman.d/gnupg'
            if keyring.is_dir():
                try:
                    # pacstrap -K can leave this invocation's gpg-agent holding
                    # its destination keyring open. Never kill live/user agents.
                    tool(['gpgconf', '--homedir', keyring, '--kill', 'gpg-agent'])
                except (Refused, OSError) as error:
                    cleanup_errors.append(error)
            for enabled, command in [(mounted_esp, ['umount', root/'boot']),
                                     (mounted_root, ['umount', root]),
                                     (bool(mapper), ['cryptsetup', 'close', mapper])]:
                if enabled:
                    try:
                        tool(command)
                    except (Refused, OSError) as error:
                        cleanup_errors.append(error)
            if cleanup_errors:
                print('Destination cleanup failed; workspace retained at '+workspace, file=sys.stderr)
                raise Refused('Destination could not be fully unmounted; do not restart automatically')
            shutil.rmtree(work)
    except BaseException:
        # Any interrupted operation retains evidence. The nested cleanup above
        # owns mount disposal; never attempt another recursive deletion here.
        raise
    progress.update('finish', 1., 'Installation completed; restart only when you choose')
    progress.send({'result': 'ok'})


def install(config, confirm, progress):
    if confirm != config['disk']:
        raise Refused('--confirm-erase must exactly match the config disk; nothing changed')
    host = require_live_vm()
    expected = select_disk(config, inventory(), host)
    if not Path('/sys/firmware/efi').is_dir():
        raise Refused('The VM must boot the live media under UEFI')
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    credentials = read_secrets(sys.stdin.buffer, config['encrypt'])
    try:
        validate_locale_and_keymap(config)
        kind = installation_source_kind()
        if kind == 'offline-repository':
            progress.update('validate', 0., 'Checking offline package integrity before erasing anything')
            validate_repo()
            perform_install(config, expected, credentials, progress)
        else:
            progress.update('validate', 0., 'Checking the immutable live image before erasing anything')
            with authenticated_live_root() as source:
                perform_install(config, expected, credentials, progress, source=source)
    finally:
        credentials.clear()  # Lifetime reduction, not a claim of Python memory zeroization.


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    disks = sub.add_parser('disks'); disks.add_argument('--json', action='store_true')
    options = sub.add_parser('choices'); options.add_argument('--json', action='store_true')
    planning = sub.add_parser('plan'); planning.add_argument('--config', required=True)
    planning.add_argument('--json', action='store_true')
    applying = sub.add_parser('install'); applying.add_argument('--config', required=True)
    applying.add_argument('--confirm-erase', required=True); applying.add_argument('--progress-fd', required=True, type=int)
    args = parser.parse_args(argv)
    progress = None
    try:
        if args.action == 'install':
            def interrupted(signum, frame):
                raise Refused('Installation interrupted; destination cleanup will be attempted')
            signal.signal(signal.SIGTERM, interrupted)
            signal.signal(signal.SIGINT, interrupted)
            progress = Progress(args.progress_fd)
        if args.action == 'disks':
            host = require_live_vm()  # Before inventory(): its findmnt fails outright without the boot mount.
            print(json.dumps(public_disks(installable_disks(inventory(), host))))
        elif args.action == 'choices':
            require_live_vm()
            print(json.dumps(choices()))
        else:
            config = validate_config(read_config(args.config))
            if args.action == 'plan':
                host = require_live_vm()
                print(json.dumps(plan(config, inventory(), host)))
            else:
                install(config, args.confirm_erase, progress)
        return 0
    except Exception as error:
        message = str(error) if isinstance(error, Refused) else 'Cannot complete '+args.action+'; check live-media inputs and diagnostic stderr'
        if not isinstance(error, Refused):
            traceback.print_exc()  # The diagnostic the message above points to.
        if progress:
            try:
                progress.send({'result': 'error', 'message': message, 'step': progress.step})
            except OSError:
                pass  # A caller that closed the progress pipe cannot receive JSON.
        elif args.action == 'plan':
            print(json.dumps({'ok': False, 'erases': [], 'steps': [], 'warnings': [message]}))
        print(message, file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
