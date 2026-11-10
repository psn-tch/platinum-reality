#!/usr/bin/env python3
"""Read-only engine contract tests with synthetic block graphs; no real devices."""
import importlib.util
import io
import json
import hashlib
import os
import shutil
import subprocess
from types import SimpleNamespace
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location('engine', Path(__file__).with_name('engine.py'))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)


def node(path, number, kind='disk', **changes):
    return {'path': path, 'maj:min': number, 'type': kind, 'size': 32*1024**3,
            'ro': False, 'rm': False, 'model': 'Owned virtual fixture', 'tran': 'virtio',
            'mountpoints': [None], **changes}


def media(number='11:0'):
    return {'filesystems': [{'target': str(engine.BOOT_MOUNT), 'maj:min': number}]}


def config():
    return {'version': 1, 'disk': '/dev/vda', 'hostname': 'platinum', 'timezone': 'UTC',
            'locale': 'en_US.UTF-8', 'keymap': 'us',
            'user': {'name': 'tester', 'full_name': 'VM Test'},
            'encrypt': True, 'network': 'iwd-networkd'}


original_validation = engine.validate_boot_assets


class ContractChecks(unittest.TestCase):
    def choices_fixture(self, root):
        definitions = root/'locale.gen'
        definitions.write_text('# prose comment\n#en_US.UTF-8 UTF-8\nen_US ISO-8859-1\n'
                               '#de_DE.UTF-8 UTF-8 # commented entry\n#bogus.UTF-8 UTF-8\n'
                               '#bad;locale UTF-8\n#bad_encoding UTF-8;evil\n')
        supported = root/'SUPPORTED'
        supported.write_text('en_US.UTF-8 UTF-8\nen_US ISO-8859-1\nde_DE.UTF-8 UTF-8\n'
                             'bad;locale UTF-8\nbad_encoding UTF-8;evil\n')
        maps = root/'keymaps'
        (maps/'i386').mkdir(parents=True)
        (maps/'i386/us.map.gz').write_bytes(b'package fixture, not executed')
        (maps/'i386/de.map').write_bytes(b'package fixture, not executed')
        (maps/'i386/us.map').write_bytes(b'duplicate name')
        (maps/'bad.map.gz').mkdir()
        (maps/'bad name.map').write_bytes(b'excluded')
        (maps/'escape.map').symlink_to(supported)
        (maps/'broken.map.gz').symlink_to(maps/'absent')
        (maps/'loop.map').symlink_to(maps/'loop.map')
        (maps/'alias.map').symlink_to(maps/'i386/us.map')
        zones = root/'zoneinfo'
        (zones/'Europe').mkdir(parents=True)
        (zones/'UTC').write_bytes(b'TZif synthetic')
        (zones/'Europe/Berlin').write_bytes(b'TZif synthetic')
        (zones/'Etc').mkdir()
        (zones/'Etc/GMT+1').write_bytes(b'TZif synthetic')
        (zones/'not-a-zone').write_bytes(b'not compiled data')
        (zones/'outside').symlink_to(supported)
        (zones/'GMT').symlink_to(zones/'UTC')
        (zones/'broken').symlink_to(zones/'missing')
        (zones/'loop').symlink_to(zones/'loop')
        return {'locale_gen': definitions, 'supported': supported, 'keymaps': maps, 'zoneinfo': zones}

    def test_choices_are_sorted_unique_readonly_and_match_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paths = self.choices_fixture(root)
            def fingerprint():
                return {str(p.relative_to(root)): ('symlink', str(p.readlink())) if p.is_symlink()
                        else ('file', hashlib.sha256(p.read_bytes()).hexdigest())
                        for p in root.rglob('*') if p.is_symlink() or p.is_file()}
            before = fingerprint()
            with patch.object(engine, 'tool') as tool, patch.object(engine, 'inventory') as disks:
                value = engine.choices(**paths)
            tool.assert_not_called(); disks.assert_not_called()
            self.assertEqual(value, {
                'locales': ['C.UTF-8', 'de_DE.UTF-8', 'en_US', 'en_US.UTF-8'],
                'timezones': ['Etc/GMT+1', 'Europe/Berlin', 'GMT', 'UTC'],
                'keymaps': ['alias', 'de', 'us']})
            validation = {key: path for key, path in paths.items() if key != 'zoneinfo'}
            for locale in value['locales']:
                for zone in value['timezones']:
                    for keymap in value['keymaps']:
                        candidate = config() | {'locale': locale, 'timezone': zone, 'keymap': keymap}
                        self.assertEqual(engine.validate_config(candidate, zoneinfo=paths['zoneinfo']), candidate)
                        engine.validate_locale_and_keymap(candidate, **validation)
            self.assertEqual(fingerprint(), before)

    def test_choices_command_contract_and_live_guard_precede_discovery(self):
        value = {'locales': ['C.UTF-8'], 'timezones': ['UTC'], 'keymaps': ['us']}
        with patch.object(engine, 'require_live_vm') as guard, \
                patch.object(engine, 'choices', return_value=value) as inventory, \
                patch.object(engine.sys, 'stdout', new_callable=io.StringIO) as output:
            self.assertEqual(engine.main(['choices', '--json']), 0)
            self.assertEqual(json.loads(output.getvalue()), value)
        guard.assert_called_once_with(); inventory.assert_called_once_with()
        with patch.object(engine, 'require_live_vm', side_effect=engine.Refused('VM guard')), \
                patch.object(engine, 'choices') as inventory, \
                patch.object(engine.subprocess, 'run') as tool, \
                patch.object(engine.sys, 'stdout', new_callable=io.StringIO) as output, \
                patch.object(engine.sys, 'stderr', new_callable=io.StringIO):
            self.assertEqual(engine.main(['choices', '--json']), 2)
            self.assertEqual(output.getvalue(), '')
        inventory.assert_not_called(); tool.assert_not_called()

    def test_unavailable_locale_and_keymap_are_not_accepted(self):
        with tempfile.TemporaryDirectory() as folder:
            paths = self.choices_fixture(Path(folder))
            validation = {key: path for key, path in paths.items() if key != 'zoneinfo'}
            for changes in [{'locale': 'prose'}, {'locale': 'bogus.UTF-8'},
                            {'keymap': 'escape'}, {'keymap': 'bad'}, {'keymap': 'broken'},
                            {'keymap': 'loop'}, {'keymap': 'missing'}]:
                with self.subTest(changes=changes), self.assertRaises(engine.Refused):
                    engine.validate_locale_and_keymap(config() | changes, **validation)

    def test_locale_encoding_ambiguity_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            paths = self.choices_fixture(Path(folder))
            paths['locale_gen'].write_text('en_US UTF-8\nen_US ISO-8859-1\n')
            paths['supported'].write_text('en_US UTF-8\nen_US ISO-8859-1\n')
            with self.assertRaisesRegex(engine.Refused, 'Ambiguous'):
                engine.choices(**paths)

    def test_offline_package_must_have_the_theme_before_installation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package = root/'platinum-reality-0.1.0-1-x86_64.pkg.tar.zst'
            package.write_bytes(b'synthetic package, parsed only by the mocked tool')
            (root/'provenance.json').write_text(json.dumps({'packages': [{
                'file': package.name, 'size_bytes': package.stat().st_size,
                'sha256': hashlib.sha256(package.read_bytes()).hexdigest()}]}))
            for missing in (None, 'Main.qml'):
                payload = '\n'.join('./'+name for name in engine.SDDM_THEME_FILES
                                    if missing is None or not name.endswith('/'+missing)).encode()
                def tool(args, **kwargs):
                    if args[0:2] == ['bsdtar', '-xOf']:
                        return b'pkgname = platinum-reality\n'
                    if args[0:2] == ['bsdtar', '-tf']:
                        return payload
                    self.fail('Unexpected command in read-only package validation')
                with patch.object(engine, 'tool', side_effect=tool), \
                        patch.object(engine, 'TARGET_PACKAGES', ['platinum-reality']), \
                        patch.object(engine, 'validate_boot_assets'):
                    if missing:
                        with self.assertRaisesRegex(engine.Refused, 'nothing erased'):
                            engine.validate_repo(root)
                    else:
                        engine.validate_repo(root)

    def boot_fixture(self, root):
        assets = root/'assets'; theme = root/'theme'; theme.mkdir()
        source = Path(__file__).resolve().parents[2]
        for name in engine.BOOT_ASSET_FILES:
            destination = assets/name; destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source/name, destination)
        (assets/'sha256.json').write_text(json.dumps({name: hashlib.sha256((assets/name).read_bytes()).hexdigest()
                                                    for name in engine.BOOT_ASSET_FILES}))
        spec = importlib.util.spec_from_file_location('fixture_splash', assets/'scripts/boot-splash.py')
        helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
        for name in helper.RENDERED:
            (theme/name).write_bytes(b'bounded fixture; root never decodes images')
        (theme/'manifest.json').write_text(json.dumps({'panel_width': 1024, 'panel_height': 768, 'icon': 'original'}))
        (theme/'platinum-reality.plymouth').write_bytes((assets/'packaging/plymouth/platinum-reality.plymouth').read_bytes())
        (theme/'platinum-reality.script').write_text('filled fixture')
        return assets, theme

    def test_boot_preflight_checks_helper_hash_and_all_rendered_assets(self):
        with tempfile.TemporaryDirectory() as folder:
            assets, theme = self.boot_fixture(Path(folder))
            helper = engine.validate_boot_assets(assets, theme)
            self.assertIn('bullet-8.png', helper.RENDERED)
            missing = theme/'bullet-8.png'; original = missing.read_bytes(); missing.unlink()
            with self.assertRaisesRegex(engine.Refused, 'nothing erased'):
                engine.validate_boot_assets(assets, theme)
            missing.write_bytes(original)
            script = assets/'scripts/boot-splash.py'; script.write_text(script.read_text()+'# changed\n')
            with self.assertRaisesRegex(engine.Refused, 'checksum mismatch'):
                engine.validate_boot_assets(assets, theme)

    def test_a_busy_partition_table_after_an_interrupted_install_is_retried(self):
        # Sammy, 2026-11-01: retrying on a disk left by an interrupted install, BLKRRPART failed EBUSY
        # while udev still probed the old ESP and LUKS partitions. Settle, then retry briefly.
        calls = []
        answers = iter([False, False, True])
        def fake_tool(argv, **kwargs):
            calls.append(list(map(str, argv)))
            if argv[0] == 'blockdev' and not next(answers):
                raise engine.Refused('Installation command failed: blockdev')
        with patch.object(engine, 'tool', side_effect=fake_tool), patch.object(engine.subprocess, 'run'), \
                patch.object(engine.time, 'sleep'):
            self.assertEqual(engine.reread_partition_table('/dev/vda'), 3)
        self.assertEqual(calls[0], ['udevadm', 'settle'])
        self.assertEqual(sum(call[0] == 'blockdev' for call in calls), 3)
        def always_busy(argv, **kwargs):
            if argv[0] == 'blockdev':
                raise engine.Refused('Installation command failed: blockdev')
        with patch.object(engine, 'tool', side_effect=always_busy), patch.object(engine.subprocess, 'run'), \
                patch.object(engine.time, 'sleep'):
            with self.assertRaises(engine.Refused) as busy:
                engine.reread_partition_table('/dev/vda', attempts=3, delay=0)
        self.assertIn('already erased', str(busy.exception))

    def test_a_refusal_after_erasing_never_says_nothing_was_erased(self):
        # install_boot_theme checks the assets again after partition/format (Sammy's review, 2026-10-30).
        with tempfile.TemporaryDirectory() as folder:
            assets, theme = self.boot_fixture(Path(folder))
            (theme/'bullet-8.png').unlink()
            with self.assertRaises(engine.Refused) as before:
                engine.validate_boot_assets(assets, theme)
            self.assertIn('nothing erased', str(before.exception))
            with self.assertRaises(engine.Refused) as after:
                engine.validate_boot_assets(assets, theme, erased=True)
            self.assertIn('already erased', str(after.exception))
            self.assertNotIn('nothing erased', str(after.exception))
            with patch.object(engine, 'BOOT_ASSETS', assets), patch.object(engine, 'BOOT_THEME', theme), \
                    patch.object(engine, 'validate_boot_assets',
                                 wraps=lambda **options: original_validation(assets, theme, **options)), \
                    patch.object(engine, 'tool') as tool:
                with self.assertRaisesRegex(engine.Refused, 'already erased'):
                    engine.install_boot_theme(Path(folder)/'target')
                tool.assert_not_called()

    def test_boot_preflight_refuses_imported_artwork_placeholders_and_symlinks(self):
        for changed in ('imported', 'placeholder', 'symlink', 'geometry'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as folder:
                assets, theme = self.boot_fixture(Path(folder))
                if changed in ('imported', 'geometry'):
                    manifest = json.loads((theme/'manifest.json').read_text())
                    manifest['icon' if changed == 'imported' else 'panel_width'] = 'imported' if changed == 'imported' else 0
                    (theme/'manifest.json').write_text(json.dumps(manifest))
                elif changed == 'placeholder':
                    (theme/'platinum-reality.script').write_text('@UNFILLED@')
                else:
                    (theme/'panel.png').unlink(); (theme/'panel.png').symlink_to(theme/'tile-1.png')
                with self.assertRaises(engine.Refused):
                    engine.validate_boot_assets(assets, theme)

    def test_actual_splash_helper_targets_only_fixture_and_hooks_survive_dropin_order(self):
        with tempfile.TemporaryDirectory() as folder:
            parent = Path(folder); assets, theme = self.boot_fixture(parent)
            root = parent/'target'
            for name in ('etc/mkinitcpio.conf.d', 'etc/kernel', 'etc/mkinitcpio.d', 'usr/bin'):
                (root/name).mkdir(parents=True, exist_ok=True)
            (root/'usr/bin/plymouth').touch()
            (root/'etc/mkinitcpio.conf').write_text('HOOKS=(base udev filesystems)\n')
            (root/'etc/mkinitcpio.conf.d/10-platinum-reality.conf').write_text(
                'HOOKS=(base systemd kms keyboard sd-vconsole block sd-encrypt filesystems fsck)\n')
            (root/'etc/kernel/cmdline').write_text('rd.luks.name=fixture=cryptroot root=/dev/mapper/cryptroot rw\n')
            (root/'etc/mkinitcpio.d/linux.preset').write_text('default_options="--cmdline /etc/kernel/cmdline"\n')
            calls = []
            def tool(args, **kwargs):
                calls.append([str(x) for x in args])
                if args[0] == 'python3':
                    subprocess.run([str(x) for x in args], check=True, capture_output=True)
                else:
                    self.assertEqual(args, ['arch-chroot', root, 'plymouth-set-default-theme', 'platinum-reality'])
            with patch.object(engine, 'BOOT_ASSETS', assets), patch.object(engine, 'BOOT_THEME', theme), \
                    patch.object(engine, 'validate_boot_assets', wraps=lambda **options: original_validation(assets, theme, **options)), \
                    patch.object(engine, 'tool', side_effect=tool):
                engine.install_boot_theme(root)
            self.assertIn('--no-rebuild', calls[0])
            hooks = subprocess.check_output(['bash', '-c',
                'source "$1"; for f in "$2"/*.conf; do source "$f"; done; printf "%s\\n" "${HOOKS[@]}"',
                'fixture', str(root/'etc/mkinitcpio.conf'), str(root/'etc/mkinitcpio.conf.d')], text=True).split()
            self.assertEqual(hooks.count('plymouth'), 1)
            self.assertLess(hooks.index('kms'), hooks.index('plymouth'))
            self.assertLess(hooks.index('plymouth'), hooks.index('sd-encrypt'))
            self.assertIn('splash', (root/'etc/kernel/cmdline').read_text().split())
            self.assertIn('--splash', (root/'etc/mkinitcpio.d/linux.preset').read_text())
            installed = root/'usr/share/plymouth/themes/platinum-reality'
            helper = original_validation(assets, theme)
            helper.checked_rendering(installed)
            self.assertEqual((installed/'bullet-8.png').read_bytes(), (theme/'bullet-8.png').read_bytes())
            self.assertNotIn('@', (installed/'platinum-reality.script').read_text())

    def test_welcome_marker_is_empty_owned_by_new_user_and_changes_no_other_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); (root/'etc').mkdir(); (root/'home/tester').mkdir(parents=True)
            (root/'etc/passwd').write_text('tester:x:1234:1235:Test:/home/tester:/bin/bash\n')
            other = root/'home/tester/existing'; other.write_text('keep me')
            before = {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with patch.object(engine.os, 'chown') as owner:
                engine.prepare_welcome(root, 'tester')
            marker = root/'home/tester/.config/platinum-reality/welcome-pending'
            self.assertEqual(marker.read_bytes(), b'')
            self.assertEqual(marker.stat().st_mode & 0o777, 0o644)
            self.assertEqual(marker.parent.stat().st_mode & 0o777, 0o755)
            owner.assert_any_call(marker, 1234, 1235)
            owner.assert_any_call(marker.parent, 1234, 1235)
            self.assertTrue(all((root/name).read_bytes() == value for name, value in before.items()))
            self.assertFalse((root/'home/liveuser').exists())
            (root/'etc/passwd').write_text('tester:x:0:0:Test:/root:/bin/bash\n')
            with self.assertRaises(engine.Refused):
                engine.prepare_welcome(root, 'tester')

    def graph(self, target=None):
        return engine.inventory_from({'blockdevices': [
            node('/dev/sr0', '11:0', 'rom', rm=True, ro=True), target or node('/dev/vda', '254:0')]}, media())

    def test_public_disks_contract_contains_no_internal_fields(self):
        disks = engine.public_disks(self.graph())
        fields = {'path', 'model', 'size_bytes', 'removable', 'transport', 'is_boot_medium', 'partitions'}
        self.assertEqual([set(disk) for disk in disks], [fields, fields])
        self.assertTrue(next(d for d in disks if d['path'] == '/dev/sr0')['is_boot_medium'])
        self.assertFalse(next(d for d in disks if d['path'] == '/dev/vda')['is_boot_medium'])

    def inventory_command_fixture(self, mounted=False):
        part=node('/dev/vda1','254:1','part',fstype='vfat',label='VM_DATA',size=1024**3,
                  mountpoints=['/mnt/occupied'] if mounted else [None])
        whole=node('/dev/vda','254:0')
        boot=node('/dev/sr0','11:0','rom',rm=True,ro=True)
        def command(argv):
            if argv[0]=='findmnt':return media()
            self.assertEqual(argv[0],'lsblk')
            # Real lsblk emits flat JSON when neither NAME nor --tree is
            # requested. PATH alone does not enable the children[] hierarchy.
            tree='--tree' in argv or 'NAME' in argv[argv.index('--output')+1].split(',')
            return {'blockdevices':[boot,whole|{'children':[part]}] if tree else [boot,whole,part]}
        return command

    def test_inventory_command_retains_existing_partition_relationships(self):
        with patch.object(engine,'read_json_command',side_effect=self.inventory_command_fixture()):
            value=engine.plan(config(),engine.inventory())
        self.assertEqual(value['existing_partitions'],[{'path':'/dev/vda1','fstype':'vfat',
            'label':'VM_DATA','size_bytes':1024**3}])
        self.assertTrue(any('every listed partition will be removed' in warning for warning in value['warnings']))

    def test_inventory_command_rejects_mounted_descendant_even_without_name_column(self):
        with patch.object(engine,'read_json_command',side_effect=self.inventory_command_fixture(mounted=True)):
            with self.assertRaisesRegex(engine.Refused,'unmounted whole disk'):
                engine.select_disk(config(),engine.inventory())

    def test_partitioned_usb_is_marked_and_refused_as_boot_medium(self):
        part = node('/dev/sda1', '8:1', 'part', fstype='vfat', label='ARCHISO', mountpoints=[str(engine.BOOT_MOUNT)])
        disks = engine.inventory_from({'blockdevices': [node('/dev/sda', '8:0', children=[part])]}, media('8:1'))
        self.assertTrue(disks[0]['is_boot_medium'])
        self.assertEqual(disks[0]['partitions'][0]['label'], 'ARCHISO')
        with self.assertRaisesRegex(engine.Refused, 'boot medium'):
            engine.select_disk(config() | {'disk': '/dev/sda'}, disks)

    def test_boot_cd_refused_before_readonly_check(self):
        with self.assertRaisesRegex(engine.Refused, 'boot medium'):
            engine.select_disk(config() | {'disk': '/dev/sr0'}, self.graph())

    def test_unknown_loop_or_missing_mount_fails_closed(self):
        for graph, mount in [({'blockdevices': [node('/dev/loop0', '7:0', 'loop')]}, media('7:0')),
                             ({'blockdevices': []}, media()), ({'blockdevices': []}, {'filesystems': []})]:
            with self.subTest(graph=graph), self.assertRaises(engine.Refused):
                engine.inventory_from(graph, mount)

    def test_mounted_children_swap_readonly_and_small_disks_refused(self):
        for changed in [{'ro': True}, {'size': 2*1024**3}, {'mountpoints': ['[SWAP]']},
                        {'children': [node('/dev/vda1', '254:1', 'part', mountpoints=['/mnt/old'])]}]:
            with self.subTest(changed=changed), self.assertRaises(engine.Refused):
                engine.select_disk(config(), self.graph(node('/dev/vda', '254:0', **changed)))

    def test_plan_is_pure_and_declares_only_confirmed_whole_disk(self):
        with patch.object(engine.subprocess, 'run') as command:
            value = engine.plan(config(), self.graph())
        command.assert_not_called()
        self.assertTrue(value['ok'])
        self.assertEqual(value['erases'], ['/dev/vda'])
        self.assertEqual([s['id'] for s in value['steps']], [s[0] for s in engine.STEPS])

    def test_small_target_install_refuses_before_secrets_or_disk_tools(self):
        disks=self.graph(node('/dev/vda','254:0',size=8*1024**3))
        with patch.object(engine,'require_live_vm'), patch.object(engine,'inventory',return_value=disks), \
                patch.object(engine,'tool') as command, patch.object(engine,'read_secrets') as secrets, \
                patch.object(engine,'perform_install') as writing:
            with self.assertRaisesRegex(engine.Refused,'at least 16 GiB'):
                engine.install(config(),'/dev/vda',Mock())
        command.assert_not_called();secrets.assert_not_called();writing.assert_not_called()

    def test_plan_preserves_existing_partition_details_and_warns_without_os_guessing(self):
        part=node('/dev/vda1','254:1','part',fstype='ntfs',label='FOREIGN_OS',size=8*1024**3)
        disks=self.graph(node('/dev/vda','254:0',children=[part]))
        before=json.dumps(disks,sort_keys=True)
        with patch.object(engine,'tool') as command:
            value=engine.plan(config(),disks)
        command.assert_not_called()
        self.assertEqual(json.dumps(disks,sort_keys=True),before)
        self.assertEqual(value['existing_partitions'],[{'path':'/dev/vda1','fstype':'ntfs',
            'label':'FOREIGN_OS','size_bytes':8*1024**3}])
        self.assertTrue(any('every listed partition will be removed' in warning for warning in value['warnings']))
        self.assertEqual(engine.plan(config(),self.graph())['existing_partitions'],[])

    def test_legacy_bios_install_refuses_before_secrets_or_writes(self):
        firmware=Mock();firmware.is_dir.return_value=False
        with patch.object(engine,'require_live_vm'), patch.object(engine,'inventory',return_value=self.graph()), \
                patch.object(engine,'Path',return_value=firmware) as path, \
                patch.object(engine,'tool') as command, patch.object(engine,'read_secrets') as secrets, \
                patch.object(engine,'perform_install') as writing:
            with self.assertRaisesRegex(engine.Refused,'UEFI'):
                engine.install(config(),'/dev/vda',Mock())
        path.assert_called_once_with('/sys/firmware/efi')
        command.assert_not_called();secrets.assert_not_called();writing.assert_not_called()

    def test_wrong_confirmation_refuses_before_environment_or_tools(self):
        with patch.object(engine, 'require_live_vm') as guard, patch.object(engine.subprocess, 'run') as command:
            with self.assertRaisesRegex(engine.Refused, 'exactly match'):
                engine.install(config(), '/dev/vdb', None)
        guard.assert_not_called(); command.assert_not_called()

    def test_unprivileged_host_refuses_before_any_tool(self):
        with patch.object(engine.os, 'geteuid', return_value=1000), patch.object(engine.subprocess, 'run') as command:
            with self.assertRaises(engine.Refused):
                engine.require_live_vm()
        command.assert_not_called()

    def test_containers_and_other_hypervisors_refuse_before_disk_tools(self):
        for virtual,container in [(SimpleNamespace(returncode=0,stdout='kvm'),SimpleNamespace(returncode=0)),
                                   (SimpleNamespace(returncode=1,stdout='none'),SimpleNamespace(returncode=0)),
                                   (SimpleNamespace(returncode=0,stdout='vmware'),None)]:
            results=[virtual]+([container] if container is not None else [])
            with self.subTest(virtual=virtual.stdout), patch.object(engine.os,'geteuid',return_value=0), \
                    patch.object(engine.subprocess,'run',side_effect=results) as detect, \
                    patch.object(engine,'tool') as command, patch.object(engine,'inventory') as inventory:
                with self.assertRaises(engine.Refused):engine.require_live_vm()
            command.assert_not_called();inventory.assert_not_called()
            self.assertTrue(all(call.args[0][0]=='systemd-detect-virt' for call in detect.call_args_list))

    def test_a_real_computer_booted_from_the_live_media_is_physical(self):
        mount=Mock();mount.is_mount.return_value=True
        with patch.object(engine.os,'geteuid',return_value=0), \
                patch.object(engine.subprocess,'run',side_effect=[SimpleNamespace(returncode=1,stdout='none'),
                                                                  SimpleNamespace(returncode=1)]), \
                patch.object(engine,'BOOT_MOUNT',mount), patch.object(engine.Path,'is_file',return_value=True):
            self.assertEqual(engine.require_live_vm(),'physical')

    def test_on_a_real_computer_internal_and_usb_disks_are_installable(self):
        # Lily Hewitt, 2026-11-10: the public ISO installs onto internal disks (her potato's NVMe); only the
        # live media itself stays refused.
        disks=engine.inventory_from({'blockdevices':[node('/dev/sr0','11:0','rom',rm=True,ro=True),
            node('/dev/nvme0n1','259:0',tran='nvme'),node('/dev/sda','8:0',tran='sata'),
            node('/dev/sdb','8:16',tran='usb',rm=False,model='USB SSD')]},media())
        marked={d['path']:d for d in engine.installable_disks(disks,'physical')}
        self.assertEqual({p for p,d in marked.items() if d['installable']},{'/dev/nvme0n1','/dev/sda','/dev/sdb'})
        self.assertEqual(marked['/dev/sr0']['unavailable'],'the installation disk')
        self.assertTrue({'installable','unavailable'} <= set(engine.public_disks(disks)[0]))
        for path in ('/dev/nvme0n1','/dev/sda','/dev/sdb'):
            self.assertEqual(engine.select_disk(config()|{'disk':path},disks,'physical')['path'],path)
        with self.assertRaisesRegex(engine.Refused,'live boot medium'):
            engine.select_disk(config()|{'disk':'/dev/sr0'},disks,'physical')
        # In an owned VM every whole disk except the live media stays installable.
        vm={d['path']:d for d in engine.installable_disks(engine.inventory_from({'blockdevices':[
            node('/dev/sr0','11:0','rom',rm=True,ro=True),node('/dev/vda','254:0')]},media()),'vm')}
        self.assertTrue(vm['/dev/vda']['installable'])

    def test_qemu_without_actual_live_boot_mount_refuses_before_disk_tools(self):
        mount=Mock();mount.is_mount.return_value=False
        with patch.object(engine.os,'geteuid',return_value=0), \
                patch.object(engine.subprocess,'run',side_effect=[SimpleNamespace(returncode=0,stdout='qemu'),SimpleNamespace(returncode=1)]), \
                patch.object(engine,'BOOT_MOUNT',mount), patch.object(engine,'tool') as command:
            with self.assertRaisesRegex(engine.Refused,'live media'):engine.require_live_vm()
        mount.is_mount.assert_called_once();command.assert_not_called()

    def test_config_rejects_secret_fields_and_injection_values(self):
        changes = [{'password': 'do-not-print-this'}, {'version': True}, {'disk': '/dev/vda;reboot'},
                   {'disk': '/dev/disk/by-id/x'}, {'hostname': 'bad\nroot'}, {'timezone': '../../etc/passwd'},
                   {'locale': 'en_US.UTF-8\nC'}, {'keymap': '--bad'}, {'network': 'NetworkManager'},
                   {'encrypt': 'true'}, {'user': {'name': 'root', 'full_name': 'Root'}},
                   {'user': {'name': 'tester', 'full_name': 'Name:extra'}}]
        for changed in changes:
            with self.subTest(changed=changed), self.assertRaises(engine.Refused):
                engine.validate_config(config() | changed)
        self.assertEqual(engine.validate_config(config()), config())

    def test_config_regular_file_only_and_duplicate_keys_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'; path.write_text(json.dumps(config()))
            self.assertEqual(engine.read_config(path), config())
            alias = Path(folder)/'alias'; alias.symlink_to(path)
            with self.assertRaises(OSError): engine.read_config(alias)
            path.write_text('{"version":1,"version":1}')
            with self.assertRaises(engine.Refused): engine.read_config(path)
            path.write_text('x'*16385)
            with self.assertRaises(engine.Refused): engine.read_config(path)

    def test_secrets_are_bounded_stdin_values_and_require_luks_passphrase(self):
        values = {'password': 'VmOnly-test-123', 'passphrase': 'Another-VM-only-123'}
        self.assertEqual(engine.read_secrets(io.BytesIO(json.dumps(values).encode()), True), values)
        for values in [{'password': 'VmOnly-test-123'}, {'password': 'VmOnly-test-123', 'passphrase': 'x'},
                       {'password': 'one\ntwo-three', 'passphrase': 'Another-VM-only-123'}]:
            with self.subTest(values=values), self.assertRaises(engine.Refused):
                engine.read_secrets(io.BytesIO(json.dumps(values).encode()), True)

    def test_progress_is_json_lines_on_the_supplied_descriptor(self):
        reading, writing = os.pipe()
        try:
            progress = engine.Progress(writing)
            progress.update('validate', 0, 'Checked the config')
            progress.send({'result': 'error', 'message': 'Refused', 'step': 'validate'})
            os.close(writing); writing = -1
            with os.fdopen(reading, 'r') as source:
                self.assertEqual(json.loads(source.readline())['step'], 'validate')
                self.assertEqual(json.loads(source.readline())['result'], 'error')
                self.assertEqual(source.read(), '')
            reading = -1
        finally:
            for fd in (reading, writing):
                if fd >= 0: os.close(fd)

    def test_failed_unmount_never_recursively_deletes_destination_and_attempts_all_cleanup(self):
        with tempfile.TemporaryDirectory() as parent:
            work = Path(parent)/'owned-work'; work.mkdir()
            calls = []
            def tool(args, **kwargs):
                calls.append([str(x) for x in args])
                if args[0] == 'lsblk': return b'/dev/vda1 1\n/dev/vda2 2\n'
                if args[0] == 'blkid': return b'OWNED-TEST-UUID\n'
                if args[0] == 'pacstrap': raise engine.Refused('Synthetic package failure')
                if args[0] == 'umount' and str(args[1]).endswith('/boot'):
                    raise engine.Refused('Synthetic busy mount')
            with patch.object(engine, 'tool', side_effect=tool), \
                    patch.object(engine, 'checked_device'), \
                    patch.object(engine.tempfile, 'mkdtemp', return_value=str(work)), \
                    patch.object(engine.shutil, 'rmtree') as deletion:
                with self.assertRaisesRegex(engine.Refused, 'fully unmounted'):
                    engine.perform_install(config() | {'encrypt': False}, {}, {}, Mock())
                deletion.assert_not_called()
            self.assertTrue(work.exists())
            self.assertEqual([c for c in calls if c[0] == 'umount'],
                             [['umount', str(work/'target/boot')], ['umount', str(work/'target')]])

    def test_a_usb_target_is_portable_without_autodetect(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ['etc', 'boot', 'usr/share/zoneinfo', 'usr/lib/modules/test-kernel', 'var/lib/sddm']:
                (root/name).mkdir(parents=True, exist_ok=True)
            (root/'etc/passwd').write_text('sddm:x:222:222:SDDM:/var/lib/sddm:/usr/bin/nologin\n'
                                           'tester:x:1000:1000:VM Test:/home/tester:/bin/bash\n')
            (root/'home/tester').mkdir(parents=True)
            (root/'usr/share/zoneinfo/UTC').write_bytes(b'TZif')
            (root/'usr/lib/modules/test-kernel/pkgbase').write_text('linux\n')
            (root/'usr/lib/modules/test-kernel/vmlinuz').write_bytes(b'fake-kernel')
            for name in engine.SDDM_THEME_FILES:
                (root/name).parent.mkdir(parents=True, exist_ok=True)
                (root/name).write_text('theme fixture, never executed')
            with patch.object(engine, 'tool'), patch.object(engine.os, 'chown'), \
                    patch.object(engine, 'install_boot_theme'), \
                    patch.object(engine, 'locale_definitions', return_value={'en_US.UTF-8': 'UTF-8'}):
                engine.configure_target(root, config(), {'password': 'Literal-validation-only', 'passphrase': 'x'*12},
                                        'ROOT-UUID', 'ESP-UUID', 'LUKS-UUID', portable=True)
            hooks = (root/'etc/mkinitcpio.conf.d/10-platinum-reality.conf').read_text()
            self.assertNotIn('autodetect', hooks)
            self.assertIn('sd-encrypt', hooks)

    def test_target_configuration_keeps_secrets_on_stdin_and_encryption_hooks_active(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ['etc', 'boot', 'usr/share/zoneinfo', 'usr/lib/modules/test-kernel', 'var/lib/sddm']:
                (root/name).mkdir(parents=True, exist_ok=True)
            (root/'etc/passwd').write_text('sddm:x:222:222:SDDM:/var/lib/sddm:/usr/bin/nologin\n'
                                           'tester:x:1000:1000:VM Test:/home/tester:/bin/bash\n')
            (root/'home/tester').mkdir(parents=True)
            (root/'usr/share/zoneinfo/UTC').write_bytes(b'TZif')
            (root/'usr/lib/modules/test-kernel/pkgbase').write_text('linux\n')
            (root/'usr/lib/modules/test-kernel/vmlinuz').write_bytes(b'fake-kernel')
            for name in engine.SDDM_THEME_FILES:
                (root/name).parent.mkdir(parents=True, exist_ok=True)
                (root/name).write_text('theme fixture, never executed')
            credentials = {'password': 'Literal-validation-only', 'passphrase': 'Unused-in-this-pure-config-test'}
            with patch.object(engine, 'tool') as command, patch.object(engine.os, 'chown'), \
                    patch.object(engine, 'install_boot_theme'), \
                    patch.object(engine, 'locale_definitions', return_value={'en_US.UTF-8': 'UTF-8'}):
                engine.configure_target(root, config(), credentials, 'ROOT-UUID', 'ESP-UUID', 'LUKS-UUID')
            for call in command.call_args_list:
                self.assertNotIn(credentials['password'], str(call.args))
                self.assertNotIn(credentials['passphrase'], str(call.args))
            passwd = [c for c in command.call_args_list if 'chpasswd' in c.args[0]]
            self.assertEqual(passwd[0].kwargs['input'], b'tester:Literal-validation-only\n')
            preset = (root/'etc/mkinitcpio.d/linux.preset').read_text()
            self.assertNotIn('ALL_config=', preset)
            self.assertIn('--cmdline /etc/kernel/cmdline', preset)
            self.assertIn('sd-encrypt', (root/'etc/mkinitcpio.conf.d/10-platinum-reality.conf').read_text())
            # Installed on a fixed disk: only that computer's drivers (autodetect).
            self.assertIn(' autodetect ', (root/'etc/mkinitcpio.conf.d/10-platinum-reality.conf').read_text())
            self.assertIn('rd.luks.name=LUKS-UUID=cryptroot', (root/'etc/kernel/cmdline').read_text())
            self.assertNotIn('archiso', (root/'etc/kernel/cmdline').read_text())
            self.assertFalse(list(root.rglob('displays.json')))
            self.assertFalse(list(root.rglob('*autologin*')))
            self.assertNotIn('NOPASSWD', (root/'etc/sudoers.d/20-platinum-wheel').read_text())
            self.assertEqual((root/'etc/locale.gen').read_text(), 'en_US.UTF-8 UTF-8\n')
            self.assertEqual((root/'etc/sddm.conf.d/10-platinum-reality.conf').read_text(),
                             '[Theme]\nCurrent=platinum-reality\n')
            for path in root.rglob('*'):
                if path.is_file() and not path.is_symlink():
                    self.assertNotIn(credentials['password'].encode(), path.read_bytes())
            # The engine runs as guest root; this reused synthetic target is
            # owned by the unprivileged test runner, including its 0440 file.
            (root/'etc/sudoers.d/20-platinum-wheel').chmod(0o640)
            with patch.object(engine, 'tool'), patch.object(engine.os, 'chown'), \
                    patch.object(engine, 'install_boot_theme'), \
                    patch.object(engine, 'locale_definitions', return_value={'en_US': 'ISO-8859-1'}):
                engine.configure_target(root, config() | {'locale': 'en_US'}, credentials,
                                        'ROOT-UUID', 'ESP-UUID', 'LUKS-UUID')
            self.assertEqual((root/'etc/locale.gen').read_text(), 'en_US ISO-8859-1\n')

    def test_only_destination_keyring_agent_is_stopped_before_unmount(self):
        with tempfile.TemporaryDirectory() as parent:
            work = Path(parent)/'owned-work'; work.mkdir()
            calls = []
            def tool(args, **kwargs):
                calls.append([str(x) for x in args])
                if args[0] == 'lsblk': return b'/dev/vda1 1\n/dev/vda2 2\n'
                if args[0] == 'blkid': return b'OWNED-TEST-UUID\n'
                if args[0] == 'pacstrap':
                    (work/'target/etc/pacman.d/gnupg').mkdir(parents=True)
                    raise engine.Refused('Synthetic package interruption')
            with patch.object(engine, 'tool', side_effect=tool), patch.object(engine, 'checked_device'), \
                    patch.object(engine.tempfile, 'mkdtemp', return_value=str(work)):
                with self.assertRaisesRegex(engine.Refused, 'package interruption'):
                    engine.perform_install(config() | {'encrypt': False}, {}, {}, Mock())
            expected = ['gpgconf', '--homedir', str(work/'target/etc/pacman.d/gnupg'), '--kill', 'gpg-agent']
            self.assertIn(expected, calls)
            self.assertLess(calls.index(expected), next(i for i, call in enumerate(calls) if call[0] == 'umount'))
            self.assertFalse(work.exists())


if __name__ == '__main__':
    unittest.main()
