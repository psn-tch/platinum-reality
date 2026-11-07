#!/usr/bin/env python3
"""Read back the owned VM's installed choices, without changing its target.

Fixture helper only, never shipped to or run on an installed/user host.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def inspect_live_copy(root, config):
    """Read-only post-install hygiene assertions; never print account secrets."""
    accounts = [line.split(':') for line in (root/'etc/passwd').read_text().splitlines()]
    assert all(len(row) == 7 and row[0] != 'liveuser' for row in accounts)
    regular = [row[0] for row in accounts if 1000 <= int(row[2]) < 65534]
    assert regular == [config['user']['name']]
    shadow = dict(line.split(':', 1) for line in (root/'etc/shadow').read_text().splitlines())
    assert shadow['root'].startswith(('!', '*')) and 'liveuser' not in shadow
    forbidden = ('home/liveuser', 'root/.ssh', 'root/.gnupg',
                 'etc/sddm.conf.d/10-platinum-live.conf', 'etc/sudoers.d/10-platinum-live',
                 'etc/mkinitcpio.conf.d/archiso.conf', 'etc/ssh/sshd_config.d/10-archiso.conf',
                 'usr/bin/platinum-reality-install', 'usr/lib/platinum-reality/installer-engine',
                 'usr/lib/platinum-reality/installer-boot', 'opt/platinum-reality/repo',
                 'opt/platinum-reality/image.json', 'var/lib/cloud')
    assert not any((root/name).exists() or (root/name).is_symlink() for name in forbidden)
    sudo = [root/'etc/sudoers', *(root/'etc/sudoers.d').iterdir()]
    for policy in sudo:
        assert not policy.is_symlink() and policy.is_file()
        assert not any(re.search(r'\bNOPASSWD\s*:', line.split('#', 1)[0])
                       for line in policy.read_text().splitlines())
    for directory in ('etc/sddm.conf.d', 'etc/systemd/system'):
        for path in (root/directory).rglob('*'):
            if path.is_symlink():
                assert path.name not in {'sshd.service', 'pacman-init.service',
                                         'platinum-live-ready.service', 'etc-pacman.d-gnupg.mount'}
            elif path.is_file():
                active = '\n'.join(line.split('#', 1)[0] for line in path.read_text().splitlines())
                assert 'liveuser' not in active and '--autologin' not in active
    identity = (root/'etc/machine-id').read_text().strip()
    assert re.fullmatch(r'[0-9a-f]{32}', identity) and identity != '0'*32
    assert (root/'var/lib/dbus/machine-id').read_text().strip() == identity
    # pacman-key's initialize/check_keyring contract uses these names, not
    # GnuPG's unrelated default pubring.kbx storage layout.
    for name in ('pubring.gpg', 'trustdb.gpg'):
        keyring = root/'etc/pacman.d/gnupg'/name
        assert keyring.is_file() and not keyring.is_symlink() and keyring.stat().st_size > 0
    pacman = '\n'.join(line.split('#', 1)[0] for line in (root/'etc/pacman.conf').read_text().splitlines())
    assert 'file://' not in pacman and '[platinum-reality]' not in pacman and '[core]' in pacman
    assert 'archiso' not in (root/'etc/kernel/cmdline').read_text()
    packages = set()
    for path in (root/'var/lib/pacman/local').glob('*/desc'):
        match = re.search(r'(?m)^%NAME%\n([^\n]+)', path.read_text()); assert match
        packages.add(match[1])
    assert packages and not packages & {'archinstall', 'mkinitcpio-archiso', 'cloud-init'}
    return {'live_copy_hygiene': 'PASS: no live identity/autologin/passwordless sudo/installer/repo/cloud state; target identity/keyring/mirrors checked',
            'hhd_package_present': 'hhd' in packages}


def inspect(root, config, *, live_copy=False):
    values = {'locale': (root/'etc/locale.conf').read_text().strip(),
              'keymap': (root/'etc/vconsole.conf').read_text().strip(),
              'timezone_link': os.readlink(root/'etc/localtime')}
    assert values == {'locale': 'LANG='+config['locale'], 'keymap': 'KEYMAP='+config['keymap'],
                      'timezone_link': '../usr/share/zoneinfo/'+config['timezone']}
    user = config['user']['name']
    account = next(line.split(':') for line in (root/'etc/passwd').read_text().splitlines()
                   if line.split(':')[0] == user)
    marker = root/'home'/user/'.config/platinum-reality/welcome-pending'
    assert marker.read_bytes() == b''
    assert marker.stat().st_uid == int(account[2]) and marker.stat().st_gid == int(account[3])
    assert marker.stat().st_mode & 0o777 == 0o644
    for directory in (marker.parent, marker.parent.parent):
        assert directory.stat().st_uid == int(account[2]) and directory.stat().st_gid == int(account[3])
        assert directory.stat().st_mode & 0o777 == 0o755
    assert (root/'usr/share/plymouth/themes/platinum-reality/bullet-8.png').is_file()
    assert '@' not in (root/'usr/share/plymouth/themes/platinum-reality/platinum-reality.script').read_text()
    assert '[Theme]\nCurrent=platinum-reality' in (root/'etc/sddm.conf.d/10-platinum-reality.conf').read_text()
    hygiene = inspect_live_copy(root, config) if live_copy else {}
    return values | {'welcome_marker': 'empty, correct owner/group/modes', 'target_theme': 'complete, no placeholders'} | hygiene


def main():
    if os.geteuid() != 0 or not Path('/run/archiso/bootmnt').is_dir():
        raise SystemExit('Requires root in the owned live VM fixture')
    if run(['systemd-detect-virt', '--vm']).stdout.strip() not in (b'qemu', b'kvm'):
        raise SystemExit('Requires QEMU/KVM')
    config = json.loads(Path('/run/pr-test/config.json').read_text())
    assert config['disk'] == '/dev/vda' and config['user']['name'] == 'tester'
    policy = Path('/opt/platinum-reality/image.json')
    live_copy = policy.exists() and json.loads(policy.read_text()).get('installation_source') == 'live-squashfs-v1'
    secret = sys.stdin.buffer.read(4097)
    assert 12 <= len(secret) <= 128 and b'\n' not in secret
    mapper = 'pr-readback-'+str(os.getpid())
    opened = mounted = False
    root = Path(tempfile.mkdtemp(prefix='pr-readback-', dir='/run'))
    try:
        device = '/dev/vda2'
        if config['encrypt']:
            run(['cryptsetup', 'open', '--readonly', '--key-file', '-', device, mapper], input=secret)
            opened = True; device = '/dev/mapper/'+mapper
        run(['mount', '-o', 'ro,noload', device, str(root)])
        mounted = True
        result = inspect(root, config, live_copy=live_copy)
    finally:
        if mounted:
            run(['umount', str(root)])
        if opened:
            run(['cryptsetup', 'close', mapper])
        # A failed unmount/close raises before this; never recursively delete.
        root.rmdir()
    print(json.dumps(result))


if __name__ == '__main__':
    main()
