#!/usr/bin/env python3
"""Pure live-copy preflight controls; no disk, chroot, account tool or VM."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from contextlib import contextmanager
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('engine', Path(__file__).with_name('engine.py'))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)


def accounts():
    return {
        'passwd': 'root:x:0:0:root:/root:/bin/bash\ndaemon:x:2:2:daemon:/:/usr/bin/nologin\n'
                  'liveuser:x:1000:1000:Live User:/home/liveuser:/bin/bash\n'
                  'nobody:x:65534:65534:Unprivileged:/:/usr/bin/nologin\n',
        'shadow': 'root:!*:1::::::\ndaemon:!:1::::::\nliveuser::1::::::\nnobody:*:1::::::\n',
        'group': 'root:x:0:\ndaemon:x:2:\nwheel:x:998:liveuser,root\nliveuser:x:1000:\n',
        'gshadow': 'root:!::\ndaemon:!::\nwheel:!:liveuser,root:root,liveuser\nliveuser:!::\n'}


class LiveRootPreflightChecks(unittest.TestCase):
    def source_fixture(self, root):
        directory = root/'arch/x86_64'; directory.mkdir(parents=True)
        image = directory/'airootfs.sfs'; image.write_bytes(b'hsqs' + b'x' * 4092)
        checksum = hashlib.sha512(image.read_bytes()).hexdigest()
        (directory/'airootfs.sha512').write_text(checksum+'  airootfs.sfs\n')
        running = root/'running-engine'; running.write_bytes(b'fixture engine, never executed')
        wrapper = b'fixture wrapper, never executed'
        policy = {'version': 2, 'installation_source': 'live-squashfs-v1', 'native_source': {
            'commit': 'a' * 40, 'distribution_commit': 'b' * 40,
            'engine_sha256': hashlib.sha256(running.read_bytes()).hexdigest(),
            'wrapper_sha256': hashlib.sha256(wrapper).hexdigest()}}
        contents = {'opt/platinum-reality/image.json': json.dumps(policy).encode(),
                    'usr/lib/platinum-reality/installer-engine': running.read_bytes(),
                    'usr/bin/platinum-reality-install': wrapper,
                    'etc/pacman.conf': b'[options]\nSigLevel=Required DatabaseOptional\n[core]\nInclude=/etc/pacman.d/mirrorlist\n'}
        contents.update({'etc/'+key: value.encode() for key, value in accounts().items()})
        paths = sorted(f'var/lib/pacman/local/{name}-1-1/desc' for name in engine.TARGET_PACKAGES)
        files = sorted(engine.SDDM_THEME_FILES) + paths
        listing = ''.join('-rw-r--r-- 0/0 100 2026-11-06 18:00 squashfs-root/'+p+'\n' for p in files).encode()
        descriptions = ''.join('%NAME%\n'+name+'\n\n%VERSION%\n1-1\n\n' for name in engine.TARGET_PACKAGES).encode()
        calls = []
        def read(argv, *, capture=False, pass_fds=()):
            calls.append(argv)
            self.assertEqual(argv[0], 'unsquashfs')
            self.assertTrue(capture)
            self.assertEqual(len(pass_fds), 1)
            self.assertEqual(os.fstat(pass_fds[0]).st_ino, image.stat().st_ino)
            self.assertIn('/proc/self/fd/'+str(pass_fds[0]), argv)
            if '-lln' in argv:
                return listing
            if len(argv) == 4 and argv[-1] in contents:
                return contents[argv[-1]]
            self.assertEqual(argv[3:], paths)
            return descriptions
        return running, image, contents, read, calls

    def test_source_modes_are_explicit_and_unknown_policies_never_fall_back(self):
        with tempfile.TemporaryDirectory() as directory:
            policy = Path(directory)/'image.json'
            self.assertEqual(engine.installation_source_kind(policy), 'offline-repository')
            for obj, kind in (({'version': 1}, 'offline-repository'),
                              ({'version': 2, 'installation_source': 'live-squashfs-v1'}, 'live-squashfs')):
                policy.write_text(json.dumps(obj)); self.assertEqual(engine.installation_source_kind(policy), kind)
            for text in ('not json', '{"version":true}', '{"version":1,"version":2}',
                         '{"version":2,"installation_source":"online"}',
                         '{"version":1,"installation_source":"live-squashfs-v1"}'):
                policy.write_text(text)
                with self.assertRaises(engine.Refused):
                    engine.installation_source_kind(policy)

    def test_authenticated_descriptor_covers_metadata_and_closes_after_use(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); running, image, contents, read, calls = self.source_fixture(root)
            with patch.object(engine, 'tool', side_effect=read), patch.object(engine, 'validate_boot_assets'):
                with engine.authenticated_live_root(root, running) as source:
                    fd = source['fd']
                    self.assertEqual(source['identity'][1], image.stat().st_ino)
                    self.assertNotIn('liveuser', source['accounts']['passwd'])
                    self.assertEqual(source['policy']['version'], 2)
                with self.assertRaises(OSError):
                    os.fstat(fd)
            self.assertTrue(calls)
            self.assertEqual(image.read_bytes(), b'hsqs' + b'x' * 4092)

    def test_corrupt_or_linked_source_refuses_before_any_child_tool(self):
        for change in ('checksum', 'symlink-file', 'symlink-parent'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                root = Path(directory); running, image, contents, read, calls = self.source_fixture(root)
                if change == 'checksum':
                    image.write_bytes(b'hsqs' + b'y' * 4092)
                elif change == 'symlink-file':
                    saved = image.with_suffix('.original'); image.rename(saved); image.symlink_to(saved)
                else:
                    parent = root/'arch'; saved = root/'actual-arch'; parent.rename(saved); parent.symlink_to(saved)
                with patch.object(engine, 'tool') as tools, patch.object(engine, 'validate_boot_assets'):
                    with self.assertRaises((engine.Refused, OSError)):
                        with engine.authenticated_live_root(root, running):
                            self.fail('Refused source yielded')
                    tools.assert_not_called()

    def test_different_engine_wrapper_and_mode_refuse_without_yielding(self):
        for change in ('engine', 'wrapper', 'mode', 'account'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                root = Path(directory); running, image, contents, read, calls = self.source_fixture(root)
                if change == 'engine':
                    contents['usr/lib/platinum-reality/installer-engine'] = b'different'
                elif change == 'wrapper':
                    contents['usr/bin/platinum-reality-install'] = b'different'
                elif change == 'mode':
                    policy = json.loads(contents['opt/platinum-reality/image.json'])
                    policy['installation_source'] = 'online'
                    contents['opt/platinum-reality/image.json'] = json.dumps(policy).encode()
                else:
                    contents['etc/shadow'] = contents['etc/shadow'].replace(b'root:!*:', b'root::')
                with patch.object(engine, 'tool', side_effect=read), patch.object(engine, 'validate_boot_assets'):
                    with self.assertRaises(engine.Refused):
                        with engine.authenticated_live_root(root, running):
                            self.fail('Invalid source yielded')

    def test_checksum_record_is_exact_and_cannot_select_another_file(self):
        sha = 'a' * 128
        for separator in ('  ', ' *', '\t'):
            self.assertEqual(engine.live_root_checksum((sha + separator + 'airootfs.sfs\n').encode()), sha)
        for raw in ((sha + '  ../airootfs.sfs\n').encode(), (sha + '  other.sfs\n').encode(),
                    (sha + '  airootfs.sfs\nextra\n').encode(), b'x' * 257, b'\xff'):
            with self.assertRaises(engine.Refused):
                engine.live_root_checksum(raw)

    def test_only_live_identity_and_memberships_are_removed(self):
        original = accounts(); saved = copy.deepcopy(original)
        result = engine.live_account_records(original)
        self.assertEqual(original, saved)
        self.assertTrue(all('liveuser' not in text for text in result.values()))
        self.assertIn('root:x:0:0:', result['passwd'])
        self.assertIn('daemon:x:2:2:', result['passwd'])
        self.assertIn('wheel:x:998:root\n', result['group'])
        self.assertIn('wheel:!:root:root\n', result['gshadow'])
        self.assertIn('nobody:x:65534:65534:', result['passwd'])
        self.assertEqual(result['shadow'], 'root:!*:1::::::\ndaemon:!:1::::::\nnobody:*:1::::::\n')

    def test_wrong_accounts_unlocked_passwords_and_malformed_records_refuse(self):
        changes = [('passwd', lambda s: s + 'other:x:1001:1001:Other:/home/other:/bin/bash\n'),
                   ('passwd', lambda s: s + 'alias:x:0:0:Alias:/root:/bin/bash\n'),
                   ('passwd', lambda s: s.replace('1000:1000', '1002:1000')),
                   ('passwd', lambda s: s.replace('/home/liveuser', '/home/another')),
                   ('passwd', lambda s: s + s.splitlines()[0] + '\n'),
                   ('shadow', lambda s: s.replace('root:!*:', 'root::')),
                   ('shadow', lambda s: s.replace('daemon:!:', 'daemon:$6$known:')),
                   ('shadow', lambda s: s.replace('daemon:!:1::::::\n', '')),
                   ('group', lambda s: s.replace('liveuser:x:1000:', 'liveuser:x:12:')),
                   ('gshadow', lambda s: s + 'malformed\n')]
        for name, change in changes:
            with self.subTest(database=name, change=change):
                fixture = accounts(); fixture[name] = change(fixture[name])
                with self.assertRaises(engine.Refused):
                    engine.live_account_records(fixture)

    def test_partial_or_unbounded_databases_refuse(self):
        fixture = accounts(); del fixture['gshadow']
        with self.assertRaises(engine.Refused):
            engine.live_account_records(fixture)
        for value in ('\0', 'x' * (128 * 1024 + 1)):
            fixture = accounts(); fixture['group'] = value
            with self.assertRaises(engine.Refused):
                engine.live_account_records(fixture)

    def test_live_repo_is_removed_without_rewriting_the_official_mirrors(self):
        standard = '[options]\nSigLevel = Required DatabaseOptional\n\n[core]\nInclude = /etc/pacman.d/mirrorlist\n[extra]\nInclude = /etc/pacman.d/mirrorlist\n'
        local = '[platinum-reality]\nSigLevel = Optional TrustAll\nServer = file:///opt/platinum-reality/repo\n\n'
        source = standard.replace('[core]', local + '[core]')
        result = engine.installed_pacman_config(source)
        self.assertEqual(result, standard)
        self.assertEqual(engine.installed_pacman_config(result), result)
        self.assertIn('Required DatabaseOptional', result)

    def test_unknown_local_repo_duplicate_stanza_or_missing_core_refuses(self):
        for text in ('[other]\nServer=file:///home/builder/repo\n[core]\n',
                     '[platinum-reality]\n[platinum-reality]\n[core]\n',
                     '[extra]\nInclude=/etc/pacman.d/mirrorlist\n', '[core]\n\0'):
            with self.assertRaises(engine.Refused):
                engine.installed_pacman_config(text)

    def test_inventory_rejects_ambiguous_paths_and_preserves_exact_file_sizes(self):
        line = '-rw-r--r-- 0/0 17 2026-11-06 18:00 squashfs-root/usr/a file\n'
        self.assertEqual(engine.live_root_inventory(line.encode()), {'usr/a file': 17})
        for raw in (line + line, line.replace('usr/a file', '../escape'),
                    line.replace('usr/a file', '/escape'), line.replace('17 ', '? '), ''):
            with self.assertRaises(engine.Refused):
                engine.live_root_inventory(raw.encode())
        self.assertTrue(engine.live_path_excluded('home/any/file'))
        self.assertTrue(engine.live_path_excluded('boot'))
        self.assertFalse(engine.live_path_excluded('homegrown/file'))
        self.assertFalse(engine.live_path_excluded('usr/bin/bash'))

    def target_fixture(self, root):
        original = accounts()
        for name, data in original.items():
            path = root/'etc'/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(data)
        for name, data in (('etc/sudoers', 'root ALL=(ALL:ALL) ALL\n'),
                           ('etc/sudoers.d/10-platinum-live', 'liveuser ALL=(ALL) NOPASSWD: ALL\n'),
                           ('etc/sddm.conf.d/10-platinum-live.conf', '[Autologin]\nUser=liveuser\n'),
                           ('usr/bin/bash', 'fixture binary'),
                           ('usr/share/sddm/themes/platinum-reality/Main.qml', 'fixture theme'),
                           ('etc/ssh/ssh_host_ed25519_key', 'fixture identity')):
            path = root/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(data)
        units = root/'etc/systemd/system/multi-user.target.wants'; units.mkdir(parents=True)
        (units/'sshd.service').symlink_to('/usr/lib/systemd/system/sshd.service')
        (units/'kept.service').symlink_to('/usr/lib/systemd/system/kept.service')
        return {'accounts_original': original, 'accounts': engine.live_account_records(original),
                'pacman_config': '[options]\nSigLevel=Required DatabaseOptional\n[core]\n'}

    def test_sanitization_removes_only_known_live_state_and_keeps_source_accounts_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source = self.target_fixture(root); before = copy.deepcopy(source)
            engine.sanitize_live_target(root, source)
            self.assertEqual(source, before)
            for name in ('etc/sudoers.d/10-platinum-live', 'etc/sddm.conf.d/10-platinum-live.conf',
                         'etc/ssh/ssh_host_ed25519_key',
                         'etc/systemd/system/multi-user.target.wants/sshd.service'):
                self.assertFalse((root/name).exists()); self.assertFalse((root/name).is_symlink())
            self.assertTrue((root/'etc/systemd/system/multi-user.target.wants/kept.service').is_symlink())
            for name, data in source['accounts'].items():
                self.assertEqual((root/'etc'/name).read_text(), data)
            self.assertEqual((root/'etc/shadow').stat().st_mode & 0o777, 0o600)
            self.assertEqual((root/'tmp').stat().st_mode & 0o7777, 0o1777)
            self.assertEqual((root/'var/lib/dbus/machine-id').readlink(), Path('../../../etc/machine-id'))
            self.assertEqual((root/'etc/machine-id').read_bytes(), b'')
            self.assertEqual((root/'usr/bin/bash').read_text(), 'fixture binary')

    def test_cleanup_refuses_symlink_ancestors_unexpected_sudo_and_live_homes(self):
        for change in ('account', 'home', 'sudo', 'ancestor'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)/'target'; root.mkdir(); source = self.target_fixture(root)
                outside = Path(folder)/'outside'; outside.mkdir(); sentinel = outside/'secret'; sentinel.write_text('untouched')
                if change == 'account':
                    (root/'etc/passwd').write_text('wrong account data')
                elif change == 'home':
                    (root/'home/liveuser').mkdir(parents=True)
                elif change == 'sudo':
                    (root/'etc/sudoers.d/unknown').write_text('ALL ALL=(ALL) NOPASSWD: ALL\n')
                else:
                    (root/'var').symlink_to(outside)
                with self.assertRaises(engine.Refused):
                    engine.sanitize_live_target(root, source)
                self.assertEqual(sentinel.read_text(), 'untouched')

    def test_copy_refuses_partial_selection_or_leaked_exclusions_before_configuration(self):
        for change in ('missing', 'size', 'link', 'excluded'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)/'target'; root.mkdir()
                image = Path(folder)/'source.sfs'; image.write_bytes(b'fixture')
                with image.open('rb') as stream:
                    info = os.fstat(stream.fileno())
                    source = {'fd': stream.fileno(), 'identity': (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns),
                              'files': {'usr/bin/bash': 7}}
                    def extraction(args, **kwargs):
                        self.assertEqual(args[0], 'unsquashfs')
                        self.assertLess(args.index('-excludes'), args.index('/proc/self/fd/'+str(stream.fileno())))
                        path = root/'usr/bin/bash'; path.parent.mkdir(parents=True)
                        if change == 'size': path.write_bytes(b'short')
                        elif change == 'link': path.symlink_to(image)
                        elif change == 'excluded':
                            path.write_bytes(b'fixture'); (root/'home/liveuser').mkdir(parents=True)
                    with patch.object(engine, 'tool', side_effect=extraction) as tools, \
                            patch.object(engine, 'sanitize_live_target') as sanitize:
                        with self.assertRaises(engine.Refused):
                            engine.copy_live_root(root, source)
                        self.assertEqual(tools.call_count, 1)
                        sanitize.assert_not_called()

    def test_selected_source_is_authenticated_before_writes_and_never_falls_back(self):
        credentials = {'password': 'synthetic-only'}
        events = []
        @contextmanager
        def source():
            events.append('authenticate')
            try:
                yield {'fixture': True}
            finally:
                events.append('close')
        def apply(*args, **kwargs):
            self.assertEqual(kwargs['source'], {'fixture': True}); events.append('write')
            raise engine.Refused('Synthetic extraction interruption')
        with patch.object(engine, 'require_live_vm'), patch.object(engine, 'inventory'), \
                patch.object(engine, 'select_disk', return_value={}), patch.object(engine.Path, 'is_dir', return_value=True), \
                patch.object(engine, 'read_secrets', return_value=credentials), \
                patch.object(engine, 'validate_locale_and_keymap'), \
                patch.object(engine, 'installation_source_kind', return_value='live-squashfs'), \
                patch.object(engine, 'authenticated_live_root', side_effect=source), \
                patch.object(engine, 'perform_install', side_effect=apply), patch.object(engine, 'validate_repo') as repo:
            with self.assertRaisesRegex(engine.Refused, 'extraction interruption'):
                engine.install({'disk': '/dev/vda', 'encrypt': False}, '/dev/vda', Mock())
            repo.assert_not_called()
        self.assertEqual(events, ['authenticate', 'write', 'close'])
        self.assertEqual(credentials, {})

    def test_successful_copy_checks_inventory_then_recreates_identity_and_package_keyring(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/'target'; root.mkdir(); (root/'boot').mkdir()
            image = Path(folder)/'source.sfs'; image.write_bytes(b'fixture')
            with image.open('rb') as stream:
                info = os.fstat(stream.fileno()); calls = []
                source = {'fd': stream.fileno(), 'identity': (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns),
                          'files': {'usr/bin/bash': len('fixture binary')}}
                def tool(args, **kwargs):
                    calls.append([str(x) for x in args])
                    if args[0] == 'unsquashfs':
                        self.assertEqual(kwargs['pass_fds'], (stream.fileno(),))
                        source.update(self.target_fixture(root))
                with patch.object(engine, 'tool', side_effect=tool):
                    engine.copy_live_root(root, source)
                self.assertEqual(calls[-3:], [
                    ['systemd-machine-id-setup', '--root', str(root)],
                    ['arch-chroot', str(root), 'pacman-key', '--init'],
                    ['arch-chroot', str(root), 'pacman-key', '--populate', 'archlinux']])
                self.assertNotIn('liveuser', (root/'etc/passwd').read_text())
                self.assertEqual(image.read_bytes(), b'fixture')

    def test_source_mutation_prevents_extraction(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/'target'; root.mkdir()
            image = Path(folder)/'source.sfs'; image.write_bytes(b'fixture')
            with image.open('rb') as stream:
                info = os.fstat(stream.fileno())
                source = {'fd': stream.fileno(), 'identity': (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)}
                image.write_bytes(b'changed length')
                with patch.object(engine, 'tool') as tool:
                    with self.assertRaisesRegex(engine.Refused, 'identity changed'):
                        engine.copy_live_root(root, source)
                    tool.assert_not_called()

    def test_live_copy_interruption_cleans_mounts_without_running_pacstrap_or_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)/'owned-work'; work.mkdir(); calls = []
            def tool(args, **kwargs):
                calls.append([str(x) for x in args])
                if args[0] == 'lsblk': return b'/dev/vda1 1\n/dev/vda2 2\n'
                if args[0] == 'blkid': return b'FIXTURE-UUID\n'
            with patch.object(engine, 'tool', side_effect=tool), patch.object(engine, 'checked_device'), \
                    patch.object(engine.tempfile, 'mkdtemp', return_value=str(work)), \
                    patch.object(engine, 'copy_live_root', side_effect=engine.Refused('Interrupted extraction')), \
                    patch.object(engine, 'configure_target') as configure:
                with self.assertRaisesRegex(engine.Refused, 'Interrupted extraction'):
                    engine.perform_install({'disk': '/dev/vda', 'encrypt': False}, {}, {}, Mock(), source={})
                configure.assert_not_called()
            self.assertFalse(any(call[0] == 'pacstrap' for call in calls))
            self.assertEqual([call for call in calls if call[0] == 'umount'], [
                ['umount', str(work/'target/boot')], ['umount', str(work/'target')]])
            self.assertFalse(work.exists())

    def test_live_package_removal_waits_for_target_configuration_and_stays_offline(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)/'owned-work'; work.mkdir(); events = []
            def tool(args, **kwargs):
                call = [str(x) for x in args]; events.append(call)
                if args[0] == 'lsblk': return b'/dev/vda1 1\n/dev/vda2 2\n'
                if args[0] == 'blkid': return b'FIXTURE-UUID\n'
            def configure(root, *args):
                events.append(['configure'])
                for name in ('boot/EFI/Linux/arch-linux.efi', 'boot/EFI/BOOT/BOOTX64.EFI'):
                    path = root/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'fixture')
            progress = Mock()
            with patch.object(engine, 'tool', side_effect=tool), patch.object(engine, 'checked_device'), \
                    patch.object(engine.tempfile, 'mkdtemp', return_value=str(work)), \
                    patch.object(engine, 'copy_live_root', side_effect=lambda *args: events.append(['copy'])), \
                    patch.object(engine, 'configure_target', side_effect=configure):
                engine.perform_install({'disk': '/dev/vda', 'encrypt': False}, {}, {}, progress,
                                       source={'packages': {'archinstall', 'cloud-init', 'firefox'}})
            remove = ['arch-chroot', str(work/'target'), 'env', 'SYSTEMD_OFFLINE=1',
                      'pacman', '-Rns', '--noconfirm', 'archinstall', 'cloud-init']
            self.assertLess(events.index(['copy']), events.index(['configure']))
            self.assertLess(events.index(['configure']), events.index(remove))
            self.assertLess(events.index(remove), next(i for i, c in enumerate(events) if 'mkinitcpio' in c))
            self.assertFalse(any(c[0] == 'pacstrap' for c in events))
            progress.send.assert_called_once_with({'result': 'ok'})


if __name__ == '__main__':
    unittest.main()
