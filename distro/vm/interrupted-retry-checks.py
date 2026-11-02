#!/usr/bin/env python3
"""Synthetic files and mocked Popen/VM only; no SSH, VM or actual signals."""
import hashlib
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('retry',Path(__file__).with_name('interrupted_retry.py'))
retry=importlib.util.module_from_spec(spec);spec.loader.exec_module(retry)


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


class RetryChecks(unittest.TestCase):
    def test_unsafe_or_ambiguous_cli_combinations_refuse_before_disk_or_vm_work(self):
        spec=importlib.util.spec_from_file_location('install_proof_retry',Path(__file__).with_name('install-proof.py'))
        proof=importlib.util.module_from_spec(spec);spec.loader.exec_module(proof)
        base=['nonexistent.iso','--interrupt-retry']
        cases=[[],['--image-engine'],['--boot-proof'],
               ['--image-engine','--boot-proof','--unencrypted'],
               ['--image-engine','--boot-proof','--gui'],
               ['--image-engine','--boot-proof','--outputs','2'],
               ['--image-engine','--boot-proof','--refusal-scenario','small'],
               ['--image-engine','--boot-proof','--observe-displays']]
        for args in cases:
            with self.subTest(args=args),contextlib.redirect_stderr(io.StringIO()), \
                    patch.object(proof,'sha256') as hashes,patch.object(proof,'Guest') as guest:
                with self.assertRaises(SystemExit) as error:proof.main(base+args)
                self.assertEqual(error.exception.code,2);hashes.assert_not_called();guest.assert_not_called()

    def command(self,root,events):
        path=root/'progress.jsonl';path.write_text(''.join(json.dumps(e)+'\n' for e in events))
        process=Mock();process.poll.return_value=None
        return SimpleNamespace(output=path,process=process)

    def valid_probe(self):
        return {'package_writes_observed':True,'mounted_installer_targets':1,
                'pacman_pids':[123],'written_package_samples':[{'package_directory':'fixture-1-1','desc_bytes':19}]}

    def test_progress_ignores_incomplete_tail_and_refuses_oversize_symlink_or_invalid_events(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p=root/'progress';p.write_text('{"step":"packages"}\n{"par')
            self.assertEqual(retry.progress(p),[{'step':'packages'}])
            p.write_text('[]\n')
            with self.assertRaises(ValueError):retry.progress(p)
            p.write_bytes(b' '*1048577)
            with self.assertRaises(ValueError):retry.progress(p)
            p.unlink();p.symlink_to(root/'other')
            with self.assertRaises(ValueError):retry.progress(p)

    def test_live_writer_and_completed_package_metadata_are_both_required(self):
        for defect in ('no-writer','no-metadata','wrong-target-count','no-observation'):
            with self.subTest(defect=defect),tempfile.TemporaryDirectory() as folder:
                command=self.command(Path(folder),[{'step':'packages'}]);value=self.valid_probe()
                if defect=='no-writer':value['pacman_pids']=[]
                elif defect=='no-metadata':value['written_package_samples']=[]
                elif defect=='wrong-target-count':value['mounted_installer_targets']=2
                else:value['package_writes_observed']=False
                probe=Mock(return_value=value)
                with patch.object(retry.time,'monotonic',side_effect=[0,1]):
                    with self.assertRaises(TimeoutError):retry.wait_for_writes(command,probe,timeout=.1)
                probe.assert_called_once_with(1)

    def test_completion_exit_or_post_package_stage_is_a_missed_window_not_a_cut(self):
        for events,code in [([{'result':'ok'}],None),([{'step':'configure'}],None),([{'step':'packages'}],0)]:
            with self.subTest(events=events),tempfile.TemporaryDirectory() as folder:
                command=self.command(Path(folder),events);command.process.poll.return_value=code;probe=Mock()
                with self.assertRaisesRegex(RuntimeError,'Missed'):retry.wait_for_writes(command,probe)
                probe.assert_not_called()

    def test_progress_advancing_during_probe_cannot_count_as_a_current_write_window(self):
        with tempfile.TemporaryDirectory() as folder:
            command=self.command(Path(folder),[{'step':'packages'}])
            def probe(attempt):
                command.output.write_text('{"step":"configure"}\n');return self.valid_probe()
            with patch.object(retry.time,'monotonic',side_effect=[0,0,1]),patch.object(retry.time,'sleep'):
                with self.assertRaisesRegex(RuntimeError,'Missed'):retry.wait_for_writes(command,probe)

    def test_verified_package_write_stage_retains_exact_observation_without_signalling(self):
        with tempfile.TemporaryDirectory() as folder:
            command=self.command(Path(folder),[{'step':'packages','fraction':.3}]);value=self.valid_probe()
            result=retry.wait_for_writes(command,Mock(return_value=value))
            self.assertEqual(result['observed'],value);self.assertEqual(result['progress_events'][0]['step'],'packages')
            command.process.terminate.assert_not_called();command.process.kill.assert_not_called()

    def test_secret_input_is_only_written_to_owned_ssh_stdin_and_cleanup_is_bounded(self):
        with tempfile.TemporaryDirectory() as folder:
            guest=SimpleNamespace(path=Path(folder),ssh_argv=Mock(return_value=['ssh','owned-command']))
            process=Mock();process.poll.return_value=None
            process.wait.side_effect=[subprocess.TimeoutExpired('fixture',5),-9]
            written=[]
            process.stdin=SimpleNamespace(write=lambda value:written.append(value),flush=lambda:None,close=Mock())
            with patch.object(retry.subprocess,'Popen',return_value=process) as start:
                with retry.AsyncInstall(guest,'safe-command','fixture',b'synthetic pipe input\n'):
                    pass
            self.assertEqual(written,[b'synthetic pipe input\n'])
            self.assertEqual(start.call_args.args[0],['ssh','owned-command'])
            process.stdin.close.assert_called_once();process.terminate.assert_called_once();process.kill.assert_called_once()
            self.assertEqual((Path(folder)/'fixture.jsonl').read_bytes(),b'')
            self.assertEqual((Path(folder)/'fixture.stderr').read_bytes(),b'')

    def test_target_identity_refuses_symlinks_and_paths_outside_its_guest(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);target=root/'target.qcow2';target.write_text('synthetic fixture')
            guest=SimpleNamespace(path=root,install_target=target)
            first=retry.target_identity(guest);self.assertEqual(first['path'],str(target))
            alias=root/'alias.qcow2';alias.symlink_to(target);guest.install_target=alias
            with self.assertRaises(ValueError):retry.target_identity(guest)
            guest.path=root/'other';guest.install_target=target
            with self.assertRaises(ValueError):retry.target_identity(guest)

    def guest(self,root):
        target=root/'target.qcow2';target.write_bytes(b'SYNTHETIC qcow2-named data, not a virtual disk')
        return SimpleNamespace(path=root,install_target=target,qmp=Mock(),
                               power_cut_install_fixture=Mock(return_value={'qemu_exit':-9,'target_retained':True}))

    def test_cut_preserves_the_exact_container_and_first_phase_logs_without_claiming_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);guest=self.guest(root);(root/'serial.log').write_text('first phase')
            (root/'known_hosts').write_text('synthetic first-phase host key')
            running=self.command(root,[{'step':'packages'}]);running.process.wait.return_value=255
            context=Mock();context.__enter__=Mock(return_value=running);context.__exit__=Mock(return_value=False)
            stage={'observed':self.valid_probe(),'observed_monotonic':0};receipt={'installed':False,'read_only_guards':'PASS'}
            with patch.object(retry,'AsyncInstall',return_value=context),patch.object(retry,'wait_for_writes',return_value=stage), \
                    patch.object(retry.shutil,'disk_usage',return_value=SimpleNamespace(free=30*1024**3)):
                identity=retry.interrupt(guest,'immutable-engine ',b'fixture',Mock(),receipt,digest)
            self.assertEqual(identity,retry.target_identity(guest));guest.power_cut_install_fixture.assert_called_once()
            copy=root/'target-interrupted-evidence.qcow2';self.assertEqual(copy.read_bytes(),guest.install_target.read_bytes())
            self.assertEqual((root/'interrupted-first-attempt/serial.log').read_text(),'first phase')
            self.assertEqual((root/'interrupted-first-attempt/known_hosts').read_text(),'synthetic first-phase host key')
            self.assertFalse((root/'known_hosts').exists())
            self.assertFalse(receipt['interrupted_retry']['ok']);self.assertNotIn('reinstall_completed',receipt['interrupted_retry'])
            self.assertIn('NOT a full virtual-sector hash',receipt['interrupted_retry']['evidence_copy']['scope'])

    def test_package_stage_advancing_while_capturing_the_frame_never_kills_the_vm(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);guest=self.guest(root);running=self.command(root,[{'step':'packages'}])
            context=Mock();context.__enter__=Mock(return_value=running);context.__exit__=Mock(return_value=False)
            def capture(*args):running.output.write_text('{"step":"configure"}\n')
            guest.qmp.side_effect=capture;receipt={'installed':False,'read_only_guards':'PASS'}
            with patch.object(retry,'AsyncInstall',return_value=context), \
                    patch.object(retry,'wait_for_writes',return_value={'observed_monotonic':0}), \
                    patch.object(retry.shutil,'disk_usage',return_value=SimpleNamespace(free=30*1024**3)):
                with self.assertRaisesRegex(RuntimeError,'advanced during evidence capture'):
                    retry.interrupt(guest,'engine ',b'fixture',Mock(),receipt,digest)
            guest.power_cut_install_fixture.assert_not_called()
            self.assertFalse((root/'target-interrupted-evidence.qcow2').exists())

    def test_changed_target_or_insufficient_space_stops_before_copy_or_retry(self):
        for defect in ('identity','space'):
            with self.subTest(defect=defect),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);guest=self.guest(root)
                if defect=='identity':
                    def changed():
                        replacement=root/'replacement';replacement.write_text('new inode');replacement.replace(guest.install_target)
                        return {'qemu_exit':-9}
                    guest.power_cut_install_fixture.side_effect=changed
                running=self.command(root,[{'step':'packages'}]);context=Mock();context.__enter__=Mock(return_value=running);context.__exit__=Mock(return_value=False)
                receipt={'installed':False,'read_only_guards':'PASS'}
                with patch.object(retry,'AsyncInstall',return_value=context),patch.object(retry,'wait_for_writes',return_value={'observed_monotonic':0}), \
                        patch.object(retry.shutil,'disk_usage',return_value=SimpleNamespace(free=1)):
                    with self.assertRaises(RuntimeError):retry.interrupt(guest,'engine ',b'fixture',Mock(),receipt,digest)
                self.assertFalse((root/'target-interrupted-evidence.qcow2').exists())


if __name__=='__main__':unittest.main()
