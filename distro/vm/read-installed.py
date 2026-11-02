#!/usr/bin/env python3
"""Read back the owned VM's installed choices, without changing its target.

Fixture helper only, never shipped to or run on an installed/user host.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def inspect(root, config):
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
    return values | {'welcome_marker': 'empty, correct owner/group/modes', 'target_theme': 'complete, no placeholders'}


def main():
    if os.geteuid() != 0 or not Path('/run/archiso/bootmnt').is_dir():
        raise SystemExit('Requires root in the owned live VM fixture')
    if run(['systemd-detect-virt', '--vm']).stdout.strip() not in (b'qemu', b'kvm'):
        raise SystemExit('Requires QEMU/KVM')
    config = json.loads(Path('/run/pr-test/config.json').read_text())
    assert config['disk'] == '/dev/vda' and config['user']['name'] == 'tester'
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
        result = inspect(root, config)
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
