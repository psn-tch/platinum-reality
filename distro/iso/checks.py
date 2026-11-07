#!/usr/bin/env python3
"""Small source checks; no VM, packages, root, services or real devices."""
import ast
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import shlex
from pathlib import Path
import tarfile
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from unittest.mock import Mock

HERE = Path(__file__).resolve().parent


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), HERE/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


builder = load('build')
spec = importlib.util.spec_from_file_location('verify_iso', HERE.parent/'vm/verify-iso.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class SourceChecks(unittest.TestCase):
    def test_vendored_package_configuration_examples_are_comments_not_repositories(self):
        root_builder = load('guest-root')
        real = (HERE/'profile/pacman.conf').read_text()
        for upstream in (real, '#[core]\n' + real):
            with self.subTest(commented_core=upstream.startswith('#[core]')), tempfile.TemporaryDirectory() as folder:
                work = Path(folder); profile = work/'profile'; profile.mkdir()
                (profile/'pacman.conf').write_text(upstream)
                repo = root_builder.repository_config(work, profile)
                self.assertEqual((profile/'airootfs/etc/pacman.conf').read_text(), upstream)
                active = [line.split('#', 1)[0].strip() for line in (profile/'pacman.conf').read_text().splitlines()]
                self.assertEqual(active.count('[platinum-reality]'), 1)
                self.assertEqual(active.count('[core]'), 1)
                self.assertLess(active.index('[platinum-reality]'), active.index('[core]'))
                self.assertEqual([line for line in active if 'file://' in line], ['Server = file://'+str(repo)])

    def test_acquisition_repository_stays_outside_the_immutable_root(self):
        root_builder = load('guest-root')
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder); profile = work/'profile'; profile.mkdir()
            upstream = '[options]\nSigLevel=Required DatabaseOptional\n[core]\nInclude=/etc/pacman.d/mirrorlist\n'
            (profile/'pacman.conf').write_text(upstream)
            repo = root_builder.repository_config(work, profile)
            self.assertEqual(repo, work/'package-repository')
            self.assertFalse(repo.is_relative_to(profile/'airootfs'))
            self.assertEqual((profile/'airootfs/etc/pacman.conf').read_text(), upstream)
            build_config = (profile/'pacman.conf').read_text()
            self.assertLess(build_config.index('[platinum-reality]'), build_config.index('[core]'))
            self.assertIn('Server = file://'+str(repo), build_config)
            self.assertIn('SigLevel=Required DatabaseOptional', build_config)
            self.assertFalse((profile/'airootfs/opt/platinum-reality/repo').exists())
            package = work/'fixture.pkg.tar.zst'; package.write_bytes(b'fixture package, never installed')
            root_builder.write_image_policy(profile, {'commit': 'a'*40}, package)
            policy = json.loads((profile/'airootfs/opt/platinum-reality/image.json').read_text())
            self.assertEqual(policy['version'], 2)
            self.assertEqual(policy['installation_source'], 'live-squashfs-v1')
            self.assertEqual(policy['package_sha256'], hashlib.sha256(package.read_bytes()).hexdigest())
            self.assertNotIn(str(work), json.dumps(policy))

    def test_live_copy_builder_refuses_legacy_embedded_cache_or_builder_mirrors(self):
        root_builder = load('guest-root')
        for change in ('embedded', 'mirror', 'duplicate-core'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                work = Path(folder); profile = work/'profile'; profile.mkdir()
                config = '[options]\n[core]\n'
                if change == 'embedded': (profile/'airootfs/opt/platinum-reality/repo').mkdir(parents=True)
                elif change == 'mirror': config += 'Server=file:///home/builder/private\n'
                else: config += '[core]\n'
                (profile/'pacman.conf').write_text(config)
                with self.assertRaises(RuntimeError):
                    root_builder.repository_config(work, profile)

    def test_builder_can_discard_only_its_owned_overlay_and_keeps_seed_read_only(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            process = SimpleNamespace(pid=12345)
            def started(command, stdout, stderr):
                stdout.close()
                (root/'qmp.sock').touch()  # Readiness fixture only; no socket or QEMU.
                return process
            with patch.object(builder.owned.subprocess, 'Popen', side_effect=started):
                builder.owned.Guest(root).start()
            command = json.loads((root/'command.json').read_text())
            drives = [command[i+1] for i, value in enumerate(command) if value == '-drive']
            self.assertEqual(drives, [f'file={root}/builder.qcow2,if=virtio,format=qcow2,discard=unmap',
                                      f'file={root}/seed.iso,media=cdrom,readonly=on'])

    def test_crowded_host_refuses_before_any_builder_or_state_directory(self):
        for bootstrap in (False, True):
            with self.subTest(bootstrap=bootstrap), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                argv = ['build.py', '--base', str(root/'unused.qcow2'),
                        '--source-commit', 'example', '--date', '2026.11.01']
                if bootstrap:
                    argv.append('--bootstrap-profile')
                with patch.object(builder, 'ROOT', root), patch.object(builder.os, 'geteuid', return_value=1000), \
                        patch.object(builder, 'resolve_source_pins'), patch('sys.argv', argv), \
                        patch.object(builder.shutil, 'disk_usage', return_value=SimpleNamespace(free=20*1024**3-1)), \
                        patch.object(builder.owned, 'Guest', side_effect=RuntimeError('unexpected builder')) as guest:
                    with self.assertRaisesRegex(SystemExit, 'Preserving host free space'):
                        builder.main()
                    guest.assert_not_called()
                self.assertFalse((root/'build').exists())

    def test_finished_iso_transport_reserve_includes_the_actual_image_size(self):
        image_size = 4_000_000_000
        threshold = image_size + 8*1024**3
        with tempfile.TemporaryDirectory() as folder:
            for free, refused in ((threshold-1, True), (threshold, False)):
                with self.subTest(free=free), patch.object(builder.shutil, 'disk_usage',
                        return_value=SimpleNamespace(free=free)):
                    if refused:
                        with self.assertRaisesRegex(SystemExit, 'Preserving host free space'):
                            builder.require_host_space(Path(folder), threshold)
                    else:
                        builder.require_host_space(Path(folder), threshold)

    def test_live_wrapper_preserves_arguments_stdin_and_json_stdout_without_host_sudo(self):
        # The only executable called sudo in this test is our temporary stub.
        # This exercises the actual shell wrapper without privileges or disks.
        wrapper = HERE/'profile/airootfs/usr/bin/platinum-reality-install'
        with tempfile.TemporaryDirectory() as folder:
            sudo = Path(folder)/'sudo'
            sudo.write_text('#!/usr/bin/python3\nimport json,sys\nprint(json.dumps({"args":sys.argv[1:],"input":sys.stdin.read()}))\n')
            sudo.chmod(0o700)
            result = subprocess.run(['/bin/sh', str(wrapper), 'install', '--config', '/run/a path.json',
                                     '--progress-fd', '1'], input='bounded input-contract fixture\n',
                                    text=True, capture_output=True, check=True,
                                    env={'PATH': folder, 'LANG': 'C.UTF-8'})
            response = json.loads(result.stdout)
            self.assertEqual(response['args'], ['-n', '--', '/usr/bin/python3', '-I',
                '/usr/lib/platinum-reality/installer-engine', 'install', '--config',
                '/run/a path.json', '--progress-fd', '1'])
            self.assertEqual(response['input'], 'bounded input-contract fixture\n')
            self.assertEqual(result.stderr, '')

    def test_heavy_lease_wait_is_bounded_and_releases_descriptor(self):
        owned = builder.owned
        with tempfile.TemporaryDirectory() as folder, patch.object(owned, 'LEASE', Path(folder)/'lease'):
            lease = owned.Lease(wait=1)
            with patch.object(owned.fcntl, 'flock', side_effect=BlockingIOError), \
                    patch.object(owned.time, 'monotonic', side_effect=[0, 2]):
                with self.assertRaises(SystemExit):
                    lease.__enter__()
                self.assertTrue(lease.file.closed)
            with patch.object(owned.fcntl, 'flock', side_effect=[BlockingIOError, None]), \
                    patch.object(owned.time, 'sleep'):
                with owned.Lease(wait=1) as lease:
                    self.assertFalse(lease.file.closed)
                self.assertTrue(lease.file.closed)
            with self.assertRaises(ValueError):
                owned.Lease(wait=float('nan'))
            cancelled = owned.Lease(wait=1)
            with patch.object(owned.fcntl, 'flock', side_effect=BlockingIOError), \
                    patch.object(owned.time, 'sleep', side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    cancelled.__enter__()
                self.assertTrue(cancelled.file.closed)

    def test_installed_login_requires_recognized_account_and_reviewed_geometry(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        greeter = 'TEST USER\nSelect your user and enter password'
        self.assertTrue(module.greeter_visible(greeter))
        self.assertFalse(module.greeter_visible('unrelated password prompt'))
        self.assertTrue(module.desktop_visible('File Edit View Window Special Help Desktop'))
        self.assertFalse(module.desktop_visible(greeter))
        self.assertTrue(module.desktop_visible('File Edit View Window Special Help Finder Hard Disk Trash'))
        self.assertFalse(module.desktop_visible('File Edit View Window Special Help Enter password'))
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder)/'greeter.ppm'
            image.write_bytes(b'P6\n1280 800\n255\n')
            self.assertEqual(module.login_point(image, greeter)['x'], 765)
            with self.assertRaises(ValueError):
                module.login_point(image, 'unknown')
            image.write_bytes(b'P6\n1024 768\n255\n')
            with self.assertRaises(ValueError):
                module.login_point(image, greeter)

    def test_live_log_collection_archives_both_runs_with_actual_python_tar_stream(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); logs=root/'sessions'; logs.mkdir()
            for name in ('initial','fallback'):
                run=logs/name; run.mkdir()
                (run/'supervisor.txt').write_text(name+' supervisor')
                (run/'backend.txt').write_text(name+' backend')
            guest=Mock(); guest.path=root/'receipt'; guest.path.mkdir()
            def ssh(command, **kwargs):
                argv=shlex.split(command)
                argv[-1]=argv[-1].replace('/home/liveuser/.local/state/platinum-reality/session-runs', str(logs))
                return subprocess.run(argv, check=True, **kwargs)
            guest.ssh.side_effect=ssh
            module.collect_live_logs(guest)
            output=guest.path/'live-session-logs'
            self.assertEqual((output/'initial/supervisor.txt').read_text(), 'initial supervisor')
            self.assertEqual((output/'fallback/backend.txt').read_text(), 'fallback backend')

    def test_install_boot_wait_tolerates_serial_file_creation_after_qmp_ready(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            guest = Mock(); guest.path = Path(folder); guest.process.poll.return_value = None
            def after_poll(delay):
                (guest.path/'serial.log').write_text('PLATINUM_LIVE_READY '+json.dumps({
                    'ready': True, 'uptime_seconds': 12., 'source': 'packaged supervisor session.json'})+'\n')
            with patch.object(module.time, 'sleep', side_effect=after_poll):
                module.wait_live(guest, module.time.monotonic()+10)

    def test_platinum_greeter_is_explicit_and_uses_keyboard_without_old_click(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        text = 'Log In\nWelcome, Test User\nEnter your password:\nRestart Shut Down Log In'
        self.assertTrue(module.greeter_visible(text, 'platinum'))
        self.assertFalse(module.greeter_visible(text, 'stock'))
        self.assertFalse(module.greeter_visible('Welcome, Someone Else Enter your password Log In', 'platinum'))
        self.assertFalse(module.greeter_visible('Test User Enter password', 'platinum'))
        self.assertFalse(module.greeter_visible(text, 'unknown'))
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder)/'greeter.ppm'
            image.write_bytes(b'P6\n1280 800\n255\n')
            action = module.login_point(image, text, 'platinum')
            self.assertEqual(action['mode'], 'keyboard')
            self.assertNotIn('x', action)
            guest = Mock()
            with patch.object(module, 'type_text') as typed, patch.object(module, 'key') as key:
                module.submit_login(guest, 'synthetic-controller-memory-only', action)
            guest.qmp.assert_not_called()
            typed.assert_called_once_with(guest, 'synthetic-controller-memory-only')
            key.assert_called_once_with(guest, 'ret')
            image.write_bytes(b'P6\n640 480\n255\n')
            with self.assertRaisesRegex(ValueError, 'no password typed'):
                module.login_point(image, text, 'platinum')
            with patch.object(module, 'type_text') as typed:
                with self.assertRaises(ValueError):
                    module.submit_login(guest, 'synthetic-controller-memory-only', {'mode': 'unknown'})
                typed.assert_not_called()

    def test_native_qmp_chord_releases_all_keys_in_reverse_order(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        guest=Mock()
        with patch.object(module.time,'sleep'):
            module.key(guest,'ctrl+a')
        calls=guest.qmp.call_args_list
        self.assertEqual([e['data']['key']['data'] for e in calls[0].args[1]['events']],['ctrl','a'])
        self.assertTrue(all(e['data']['down'] for e in calls[0].args[1]['events']))
        self.assertEqual([e['data']['key']['data'] for e in calls[1].args[1]['events']],['a','ctrl'])
        self.assertTrue(all(not e['data']['down'] for e in calls[1].args[1]['events']))
        guest.reset_mock()
        with patch.object(module.time,'sleep',side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):module.key(guest,'ctrl+a')
        self.assertEqual(guest.qmp.call_count,2)

    def test_unlock_mode_requires_its_exact_prompt(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        graphical = 'Enter your password to start up.'
        plain = 'Please enter passphrase for hard disk cryptroot'
        self.assertTrue(module.unlock_visible(graphical, 'plymouth'))
        self.assertTrue(module.unlock_visible(plain, 'plain'))
        self.assertFalse(module.unlock_visible(plain, 'plymouth'))
        self.assertFalse(module.unlock_visible(graphical, 'plain'))
        self.assertFalse(module.unlock_visible('Welcome, Test User. Enter your password: Log In', 'plymouth'))
        self.assertFalse(module.unlock_visible(graphical, 'guess'))

    def test_graphical_unlock_matches_pinned_dialog_pixels_without_fuzzy_ocr(self):
        from PIL import Image, ImageDraw
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);reference=root/'reference.png';screen=root/'screen.ppm'
            dialog=Image.new('RGBA',(600,136),'white')
            ImageDraw.Draw(dialog).rectangle((0,0,599,135),outline='black',width=8)
            dialog.save(reference)
            image=Image.new('RGB',(1280,800),'gray');image.paste(dialog,(340,462));image.save(screen)
            self.assertTrue(module.graphical_unlock_visible(screen,reference))
            image.putpixel((350,480),(255,0,0));image.save(screen)
            self.assertFalse(module.graphical_unlock_visible(screen,reference))
            Image.new('RGB',(640,480)).save(screen)
            self.assertFalse(module.graphical_unlock_visible(screen,reference))
            image.save(screen);Image.new('RGB',(300,68),'white').save(reference)
            with self.assertRaisesRegex(ValueError,'no password typed'):
                module.graphical_unlock_visible(screen,reference)

    def test_gui_choice_uses_advertised_order_and_same_row_visible_value(self):
        spec = importlib.util.spec_from_file_location('gui_drive', HERE.parent/'vm/gui-drive.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        guest = Mock(); key = Mock()
        words = [{'text': 'uk', 'x': 400, 'y': 200, 'w': 14, 'h': 12, 'line': (1,1,1,1)}]
        with patch.object(module, 'wait', return_value=([], [{'left': 200, 'y': 206}])), \
                patch.object(module, 'click') as click, patch.object(module, 'capture', return_value=words):
            module.choose(guest, 'Keyboard', ['de','uk','us'], 'uk', 'uk', key)
            self.assertEqual([call.args[1] for call in key.call_args_list], ['esc','home','down','tab'])
            click.assert_called_once_with(guest, 400, 206)
            with self.assertRaises(ValueError):
                module.choose(guest, 'Keyboard', ['us'], 'uk', 'uk', key)
            words[0]['y'] = 230
            with self.assertRaises(RuntimeError):
                module.choose(guest, 'Keyboard', ['uk'], 'uk', 'uk', key)

    def test_installed_readback_proves_nondefault_choices_and_marker_without_commands(self):
        spec = importlib.util.spec_from_file_location('read_installed', HERE.parent/'vm/read-installed.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        config = {'locale': 'en_GB.UTF-8', 'keymap': 'uk', 'timezone': 'Europe/London', 'user': {'name': 'tester'}}
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            files = {'etc/locale.conf': 'LANG=en_GB.UTF-8\n', 'etc/vconsole.conf': 'KEYMAP=uk\n',
                     'etc/passwd': f'tester:x:{os.getuid()}:{os.getgid()}:Test:/home/tester:/bin/bash\n',
                     'home/tester/.config/platinum-reality/welcome-pending': '',
                     'usr/share/plymouth/themes/platinum-reality/bullet-8.png': 'fixture',
                     'usr/share/plymouth/themes/platinum-reality/platinum-reality.script': 'filled',
                     'etc/sddm.conf.d/10-platinum-reality.conf': '[Theme]\nCurrent=platinum-reality\n'}
            for name, contents in files.items():
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(contents)
            marker=root/'home/tester/.config/platinum-reality/welcome-pending'
            marker.chmod(0o644);marker.parent.chmod(0o755);marker.parent.parent.chmod(0o755)
            (root/'etc/localtime').symlink_to('../usr/share/zoneinfo/Europe/London')
            with patch.object(module, 'run') as command:
                result=module.inspect(root,config)
                command.assert_not_called()
            self.assertEqual(result['locale'],'LANG=en_GB.UTF-8')
            (root/'etc/vconsole.conf').write_text('KEYMAP=us\n')
            with self.assertRaises(AssertionError):
                module.inspect(root,config)

    def test_both_source_pins_remain_fixed_when_branch_moves_during_bootstrap(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'source';source.mkdir();evidence=root/'evidence';evidence.mkdir()
            files={'packaging/installer/platinum-reality-installer.desktop':'launcher fixture',
                   'distro/iso/UPSTREAM.md':'fixture provenance',
                   'distro/iso/profile/airootfs/usr/bin/platinum-reality-install':'wrapper fixture',
                   'distro/installer-engine/engine.py':'engine A'}
            for name,text in files.items():
                path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
            git=['git','-C',str(source),'-c','core.hooksPath=/dev/null','-c','user.name=Example','-c','user.email=example@example.invalid']
            subprocess.run(git+['init','-q','-b','main'],check=True)
            subprocess.run(git+['add','.'],check=True);subprocess.run(git+['commit','-qm','Pin A'],check=True)
            args=SimpleNamespace(source_repo=source,source_commit='main',date='2026.10.30')
            guest=Mock();guest.ssh.side_effect=RuntimeError('stop before guest execution')
            with patch.object(builder,'ROOT',source):
                builder.resolve_source_pins(args)
                original=args.source_commit
                (source/'distro/installer-engine/engine.py').write_text('engine B')
                subprocess.run(git+['add','.'],check=True);subprocess.run(git+['commit','-qm','Moved branch'],check=True)
                self.assertNotEqual(subprocess.check_output(git+['rev-parse','HEAD'],text=True).strip(),original)
                with self.assertRaisesRegex(RuntimeError,'stop before guest'):
                    builder.build_iso(args,guest,evidence)
            pin=json.loads((evidence/'pin.json').read_text())
            self.assertEqual(pin['commit'],original);self.assertEqual(pin['distribution_commit'],original)
            self.assertEqual((evidence/'distribution-source/distro/installer-engine/engine.py').read_text(),'engine A')

    def test_image_engine_is_checked_against_its_immutable_pin_not_a_new_worktree(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            files={'distro/installer-engine/engine.py':b'engine A',
                   'distro/iso/profile/airootfs/usr/bin/platinum-reality-install':b'wrapper A'}
            for name,data in files.items():
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            git=['git','-C',str(root),'-c','core.hooksPath=/dev/null','-c','user.name=Example','-c','user.email=example@example.invalid']
            subprocess.run(git+['init','-q','-b','main'],check=True)
            subprocess.run(git+['add','.'],check=True);subprocess.run(git+['commit','-qm','Image source'],check=True)
            commit=subprocess.check_output(git+['rev-parse','HEAD'],text=True).strip()
            pin={'distribution_commit':commit,'engine_sha256':hashlib.sha256(b'engine A').hexdigest(),
                 'wrapper_sha256':hashlib.sha256(b'wrapper A').hexdigest()}
            (root/'distro/installer-engine/engine.py').write_text('engine B; not in this image')
            self.assertEqual(module.image_source_hashes(pin,root)['engine_sha256'],pin['engine_sha256'])
            with self.assertRaisesRegex(ValueError,'differs from'):
                module.image_source_hashes(pin|{'engine_sha256':'0'*64},root)
            with patch.object(module.subprocess,'check_output') as command:
                with self.assertRaisesRegex(ValueError,'full, locally known'):
                    module.image_source_hashes(pin|{'distribution_commit':'main'},root)
                command.assert_not_called()

    def test_refusal_protocol_requires_whole_disk_hash_and_prewrite_message(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        for scenario,wanted in [('small','at least 16 GiB'),('bios','UEFI')]:
            for bad in ('none','changed','wrong-message','started-writing'):
                receipt={}
                def remote(command,name,**kwargs):
                    if name=='scenario-hash-before':return 'a'*64+'  /dev/vda\n'
                    if name=='scenario-hash-after':return ('b' if bad=='changed' else 'a')*64+'  /dev/vda\n'
                    self.assertEqual(kwargs,{'input':b'','expected':2})
                    events=[{'result':'error','message':wanted if bad!='wrong-message' else 'unrelated failure','step':'validate'}]
                    if bad=='started-writing':events.insert(0,{'step':'partition','fraction':0.1})
                    return '\n'.join(json.dumps(event) for event in events)
                with self.subTest(scenario=scenario,bad=bad):
                    if bad=='none':
                        module.refusal_checks(scenario,'fixed-engine ',remote,receipt)
                        self.assertTrue(receipt['ok']);self.assertEqual(receipt['full_target_hash'],'a'*64)
                    else:
                        with self.assertRaises(ValueError):module.refusal_checks(scenario,'fixed-engine ',remote,receipt)
                        self.assertFalse(receipt.get('ok',False))
        remote=Mock()
        with self.assertRaises(ValueError):module.refusal_checks('unknown','',remote,{})
        remote.assert_not_called()

    def test_occupied_protocol_needs_actual_partition_details_and_labels_gui_unqualified(self):
        spec = importlib.util.spec_from_file_location('install_proof', HERE.parent/'vm/install-proof.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        plan={'ok':True,'erases':['/dev/vda'],'existing_partitions':[{'path':'/dev/vda1','fstype':'vfat','label':'VM_DATA'}],
              'warnings':['every listed partition will be removed']}
        for correct in (True,False):
            receipt={};calls=[]
            def remote(command,name,**kwargs):
                calls.append(command)
                if name.startswith('scenario-hash-'):return 'a'*64+'  /dev/vda\n'
                if name=='occupied-plan':return json.dumps(plan if correct else plan|{'existing_partitions':[]})
                if name=='mounted-child-install-refusal':
                    self.assertEqual(kwargs,{'input':b'','expected':2})
                    return json.dumps({'result':'error','step':'validate','message':'Destination must be a writable, unmounted whole disk with no active swap'})
                if name in ('mount-occupied-readonly','unmount-occupied-readonly'):return ''
                if name in ('occupied-lsblk-version','occupied-device-flat','occupied-device-tree'):return ''
                self.assertEqual(name,'prepare-occupied-vm-target');return ''
            if correct:
                module.refusal_checks('occupied','fixed-engine ',remote,receipt)
                self.assertTrue(receipt['ok']);self.assertIn('NOT TESTED',receipt['gui_erase_confirmation'])
            else:
                with self.assertRaises(ValueError):module.refusal_checks('occupied','fixed-engine ',remote,receipt)
            if correct:self.assertTrue(receipt['mounted_child_install_refusal'])
            else:self.assertFalse(any('confirm-erase' in command for command in calls))

    def test_mounted_child_refusal_requires_prewrite_error_and_unmounts_on_failure(self):
        spec=importlib.util.spec_from_file_location('install_proof',HERE.parent/'vm/install-proof.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        for defect in ('none','wrong-message','started-writing','transport'):
            calls=[];receipt={}
            def remote(command,name,**kwargs):
                calls.append(name)
                if name!='mounted-child-install-refusal':return ''
                self.assertEqual(kwargs,{'input':b'','expected':2})
                if defect=='transport':raise RuntimeError('Synthetic transport failure')
                events=[{'result':'error','step':'validate','message':'unmounted whole disk' if defect!='wrong-message' else 'unrelated'}]
                if defect=='started-writing':events.insert(0,{'step':'partition','fraction':.1})
                return '\n'.join(json.dumps(e) for e in events)
            if defect=='none':
                module.mounted_child_refusal('fixture-engine ',remote,receipt)
                self.assertIn('mounted_child_install_refusal',receipt)
            else:
                with self.assertRaises((ValueError,RuntimeError)):module.mounted_child_refusal('fixture-engine ',remote,receipt)
                self.assertNotIn('mounted_child_install_refusal',receipt)
            self.assertEqual(calls[-1],'unmount-occupied-readonly')

    def test_ready_receipt_after_firmware_controls_and_partial_writes(self):
        marker = {'ready': True, 'uptime_seconds': 12.07, 'source': 'packaged supervisor session.json'}
        text = '\x1bP+q6E616D65\x1b\\PLATINUM_LIVE_READY ' + json.dumps(marker)+'\r\n'
        self.assertEqual(verifier.ready_marker(text), marker)
        self.assertIsNone(verifier.ready_marker(text[:text.index('source')]))
        for changed in ({'ready': False}, {'uptime_seconds': -1}, {'uptime_seconds': True},
                        {'source': 'unrelated'}, {'uptime_seconds': float('nan')}):
            with self.subTest(changed=changed):
                self.assertIsNone(verifier.ready_marker('PLATINUM_LIVE_READY '+json.dumps(marker | changed)))

    def test_live_autologin_stays_in_live_profile(self):
        text = (HERE/'profile/airootfs/etc/sddm.conf.d/10-platinum-live.conf').read_text()
        self.assertIn('User=liveuser', text)
        self.assertIn('Session=platinum-reality.desktop', text)
        self.assertFalse((HERE/'profile/airootfs/etc/systemd/system/getty@tty1.service.d/autologin.conf').exists())
        self.assertEqual((HERE/'profile/airootfs/etc/systemd/system/default.target').readlink(),
                         Path('/usr/lib/systemd/system/graphical.target'))
        self.assertNotIn('Autologin', (HERE.parents[1]/'scripts/setup-fresh-arch.py').read_text().split('def updated_sddm_state')[1].split('def preselect_sddm_session')[0].replace('# No [Autologin] configuration is created.', ''))

    def test_live_packages_cover_fresh_arch_extras_without_nm(self):
        extras = None
        for node in ast.parse((HERE.parents[1]/'scripts/setup-fresh-arch.py').read_text()).body:
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'EXTRAS' for t in node.targets):
                extras = ast.literal_eval(node.value)
        names = set((HERE/'profile/packages.x86_64').read_text().split())
        self.assertTrue(set(extras).issubset(names))
        self.assertTrue({'platinum-reality', 'plymouth', 'udisks2', 'iwd', 'systemd-ukify'}.issubset(names))
        self.assertNotIn('networkmanager', {x.lower() for x in names})
        self.assertFalse((HERE/'profile/airootfs/etc/systemd/network/20-wlan.network').exists())
        self.assertEqual((HERE/'profile/airootfs/etc/systemd/system/systemd-time-wait-sync.service').readlink(), Path('/dev/null'))

    def archive(self, root, entries):
        archive = root/'input.tar'
        with tarfile.open(archive, 'w') as tar:
            for name, kind, value in entries:
                item = tarfile.TarInfo(name)
                if kind == 'link':
                    item.type = tarfile.SYMTYPE
                    item.linkname = value
                    tar.addfile(item)
                else:
                    item.size = len(value)
                    tar.addfile(item, io.BytesIO(value))
        return archive

    def test_absolute_systemd_symlink_is_preserved_without_following(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = self.archive(root, [('releng/etc/localtime', 'link', '/usr/share/zoneinfo/UTC')])
            builder.extract_profile(archive, root/'out')
            self.assertEqual((root/'out/releng/etc/localtime').readlink(), Path('/usr/share/zoneinfo/UTC'))

    def test_archive_traversal_and_writes_through_links_are_rejected(self):
        cases = [[('../escape', 'file', b'bad')], [('/tmp/escape', 'file', b'bad')],
                 [('releng/./link', 'link', '/tmp'), ('releng/link/escape', 'file', b'bad')],
                 [('releng/a', 'file', b'one'), ('releng/./a', 'file', b'two')]]
        for entries in cases:
            with self.subTest(entries=entries), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                with self.assertRaises(ValueError):
                    builder.extract_profile(self.archive(root, entries), root/'out')
                self.assertFalse((root/'out').exists())

    def test_privileged_guest_script_refuses_host_before_commands(self):
        module = load('guest-root')
        with patch.object(module.os, 'geteuid', return_value=0), \
                patch.object(module.socket, 'gethostname', return_value='ordinary-host'), \
                patch.object(module.subprocess, 'check_output') as command:
            with self.assertRaises(SystemExit):
                module.main()
            command.assert_not_called()

    def test_unprivileged_guest_script_refuses_host_before_commands(self):
        module = load('guest-build')
        with patch('sys.argv', ['guest-build.py', '/nonexistent']), \
                patch.object(module.os, 'geteuid', return_value=1000), \
                patch.object(module.socket, 'gethostname', return_value='ordinary-host'), \
                patch.object(module.subprocess, 'check_output') as command:
            with self.assertRaises(SystemExit):
                module.main()
            command.assert_not_called()


if __name__ == '__main__':
    unittest.main()
