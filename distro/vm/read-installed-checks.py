#!/usr/bin/env python3
"""Read-only post-install hygiene controls with temporary synthetic trees."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('reader', Path(__file__).with_name('read-installed.py'))
reader = importlib.util.module_from_spec(spec); spec.loader.exec_module(reader)


def fixture(root):
    files = {'etc/passwd': 'root:x:0:0:root:/root:/bin/bash\ntester:x:1000:1000:Test:/home/tester:/bin/bash\n',
             'etc/shadow': 'root:!*:1::::::\ntester:$6$fixture:1::::::\n',
             'etc/sudoers': 'root ALL=(ALL:ALL) ALL\n# NOPASSWD: is only a comment\n',
             'etc/sudoers.d/20-wheel': '%wheel ALL=(ALL:ALL) ALL\n',
             'etc/sddm.conf.d/10-platinum.conf': '[Theme]\nCurrent=platinum-reality\n',
             'etc/machine-id': 'a'*32+'\n', 'etc/pacman.d/gnupg/pubring.gpg': 'fixture keyring',
             'etc/pacman.d/gnupg/trustdb.gpg': 'fixture trust database',
             'etc/pacman.conf': '[options]\nSigLevel=Required DatabaseOptional\n[core]\n',
             'etc/kernel/cmdline': 'root=UUID=fixture rw\n',
             'var/lib/pacman/local/base-1-1/desc': '%NAME%\nbase\n\n'}
    for name, data in files.items():
        path=root/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_text(data)
    (root/'etc/systemd/system').mkdir(parents=True)
    link=root/'var/lib/dbus/machine-id'; link.parent.mkdir(parents=True)
    link.symlink_to('../../../etc/machine-id')
    return {'user': {'name': 'tester'}}


class HygieneChecks(unittest.TestCase):
    def test_valid_target_is_inspected_without_writes_or_secret_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); config=fixture(root)
            before={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file() and not p.is_symlink()}
            result=reader.inspect_live_copy(root,config)
            after={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file() and not p.is_symlink()}
            self.assertEqual(before,after); self.assertFalse(result['hhd_package_present'])
            self.assertNotIn('$6$fixture',repr(result)); self.assertIn('PASS',result['live_copy_hygiene'])

    def test_live_home_installer_dangling_link_and_passwordless_sudo_refuse(self):
        for name,data in [('home/liveuser','directory'), ('usr/bin/platinum-reality-install','payload'),
                          ('etc/sudoers.d/10-platinum-live','liveuser ALL=(ALL) NOPASSWD: ALL\n'),
                          ('opt/platinum-reality/repo','symlink')]:
            with self.subTest(path=name), tempfile.TemporaryDirectory() as directory:
                root=Path(directory);config=fixture(root);path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                if data=='directory':path.mkdir()
                elif data=='symlink':path.symlink_to('absent')
                else:path.write_text(data)
                with self.assertRaises(AssertionError):reader.inspect_live_copy(root,config)

    def test_unexpected_live_account_unlock_identity_and_local_mirror_refuse(self):
        changes=[('etc/passwd','liveuser:x:1000:1000:Live:/home/liveuser:/bin/bash\n'),
                 ('etc/shadow','root::1::::::\n'),('etc/machine-id','0'*32+'\n'),
                 ('etc/pacman.conf','[core]\nServer=file:///opt/platinum-reality/repo\n'),
                 ('etc/kernel/cmdline','archisobasedir=arch root=UUID=fixture rw\n'),
                 ('etc/systemd/system/getty.conf','ExecStart=agetty --autologin root\n'),
                 ('var/lib/pacman/local/cloud-init-1-1/desc','%NAME%\ncloud-init\n\n')]
        for name,data in changes:
            with self.subTest(path=name), tempfile.TemporaryDirectory() as directory:
                root=Path(directory);config=fixture(root);path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(data)
                with self.assertRaises(AssertionError):reader.inspect_live_copy(root,config)

    def test_enabled_live_service_alias_refuses_without_following_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);config=fixture(root)
            (root/'etc/systemd/system/pacman-init.service').symlink_to('/usr/lib/systemd/system/pacman-init.service')
            with self.assertRaises(AssertionError):reader.inspect_live_copy(root,config)

    def test_missing_empty_or_linked_pacman_keyring_refuses(self):
        for name in ('pubring.gpg', 'trustdb.gpg'):
            for change in ('missing', 'empty', 'link'):
                with self.subTest(name=name, change=change), tempfile.TemporaryDirectory() as directory:
                    root=Path(directory);config=fixture(root);path=root/'etc/pacman.d/gnupg'/name
                    path.unlink()
                    if change=='empty':path.write_bytes(b'')
                    elif change=='link':path.symlink_to('absent')
                    with self.assertRaises(AssertionError):reader.inspect_live_copy(root,config)


if __name__=='__main__':unittest.main()
