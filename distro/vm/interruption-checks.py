#!/usr/bin/env python3
"""Read-only synthetic trees and mocked process/QMP ownership; no VM or signals."""
import importlib.util
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0,str(Path(__file__).resolve().parent))
import owned
spec=importlib.util.spec_from_file_location('write_probe',Path(__file__).with_name('package-write-probe.py'))
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)


class InterruptionChecks(unittest.TestCase):
    def fixture(self,root):
        runtime=root/'run';runtime.mkdir();proc=root/'proc';proc.mkdir()
        target=runtime/'platinum-install-test/target'
        db=target/'var/lib/pacman/local/fixture-package-1-1';db.mkdir(parents=True)
        (db/'desc').write_text('synthetic package metadata')
        (proc/'123').mkdir();(proc/'123/comm').write_text('pacman\n')
        return runtime,proc,target,db

    def test_progress_without_real_package_metadata_or_active_writer_is_not_enough(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime,proc,target,db=self.fixture(Path(folder))
            with patch.object(Path,'is_mount',autospec=True,side_effect=lambda p:p==target):
                record=probe.observe(runtime,proc)
                self.assertTrue(record['package_writes_observed'])
                self.assertEqual(record['written_package_samples'][0]['desc_bytes'],26)
                (proc/'123/comm').write_text('bash\n')
                self.assertFalse(probe.observe(runtime,proc)['package_writes_observed'])
                (proc/'123/comm').write_text('pacman\n');(db/'desc').write_text('')
                self.assertFalse(probe.observe(runtime,proc)['package_writes_observed'])

    def test_unmounted_ambiguous_and_symlinked_package_state_cannot_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime,proc,target,db=self.fixture(Path(folder))
            self.assertFalse(probe.observe(runtime,proc)['package_writes_observed'])
            other=runtime/'platinum-install-another/target';other.mkdir(parents=True)
            with patch.object(Path,'is_mount',return_value=True):
                self.assertFalse(probe.observe(runtime,proc)['package_writes_observed'])
            with patch.object(Path,'is_mount',autospec=True,side_effect=lambda p:p==target):
                (db/'desc').unlink();(db/'desc').symlink_to(proc/'123/comm')
                self.assertFalse(probe.observe(runtime,proc)['package_writes_observed'])

    def test_host_and_container_refusal_precede_proc_or_disk_observations(self):
        with patch.object(probe.os,'geteuid',return_value=1000),patch.object(probe.subprocess,'run') as commands:
            with self.assertRaises(SystemExit):probe.require_live()
            commands.assert_not_called()
        with patch.object(probe.os,'geteuid',return_value=0), \
                patch.object(probe.socket,'gethostname',return_value='platinum-live'), \
                patch.object(probe.subprocess,'run',side_effect=[
                    SimpleNamespace(returncode=0,stdout='kvm'),SimpleNamespace(returncode=0)]), \
                patch.object(Path,'is_mount') as mounts:
            with self.assertRaises(SystemExit):probe.require_live()
            mounts.assert_not_called()

    def guest(self):
        guest=owned.Guest('/owned/run');guest.is_live_iso=True;guest.pid=123
        guest.install_target=Path('/owned/run/target.qcow2')
        guest.process=Mock(pid=123);guest.process.poll.return_value=None;guest.process.wait.return_value=-9
        guest.qmp=Mock(return_value={'status':'running'})
        return guest

    def socket(self,**changes):
        return SimpleNamespace(**({'st_mode':stat.S_IFSOCK|0o600,'st_uid':os.getuid(),'st_dev':9,'st_ino':10}|changes))

    def test_only_owned_live_installation_process_can_receive_injection(self):
        for change in ('builder','exited','pid','target'):
            guest=self.guest()
            if change=='builder':guest.is_live_iso=False
            elif change=='exited':guest.process.poll.return_value=0
            elif change=='pid':guest.pid=456
            else:guest.install_target=Path('/outside/target.qcow2')
            with self.assertRaises(RuntimeError):guest.power_cut_install_fixture()
            guest.qmp.assert_not_called();guest.process.kill.assert_not_called()

    def test_authenticated_running_qmp_precedes_owned_popen_kill(self):
        guest=self.guest();calls=Mock();calls.attach_mock(guest.qmp,'qmp');calls.attach_mock(guest.process,'process')
        with patch.object(Path,'lstat',return_value=self.socket()),patch.object(Path,'unlink') as unlink:
            result=guest.power_cut_install_fixture()
        self.assertEqual(result['qemu_exit'],-9);self.assertTrue(result['target_retained'])
        self.assertEqual([call[0] for call in calls.mock_calls],['process.poll','qmp','process.kill','process.wait'])
        unlink.assert_called_once_with()

    def test_foreign_or_nonrunning_endpoint_never_kills(self):
        for wrong in ('owner','type','status'):
            guest=self.guest()
            if wrong=='status':guest.qmp.return_value={'status':'paused'}
            value=self.socket(st_uid=os.getuid()+1) if wrong=='owner' else self.socket(st_mode=stat.S_IFREG) if wrong=='type' else self.socket()
            with patch.object(Path,'lstat',return_value=value),patch.object(Path,'unlink') as unlink:
                with self.assertRaises(RuntimeError):guest.power_cut_install_fixture()
                guest.process.kill.assert_not_called();unlink.assert_not_called()

    def test_replaced_endpoint_is_retained_and_non_sigkill_exit_is_not_a_pass(self):
        guest=self.guest()
        with patch.object(Path,'lstat',side_effect=[self.socket(),self.socket(st_ino=11)]),patch.object(Path,'unlink') as unlink:
            with self.assertRaisesRegex(RuntimeError,'changed'):guest.power_cut_install_fixture()
            unlink.assert_not_called()
        guest=self.guest();guest.process.wait.return_value=0
        with patch.object(Path,'lstat',return_value=self.socket()),patch.object(Path,'unlink'):
            with self.assertRaisesRegex(RuntimeError,'SIGKILL'):guest.power_cut_install_fixture()


class BuilderShutdownChecks(unittest.TestCase):
    def guest(self):
        guest=owned.Guest('/owned/builder');guest.is_live_iso=False;guest.ssh_ready=True
        guest.process=Mock();guest.process.poll.return_value=None
        guest.ssh=Mock();guest.qmp=Mock()
        return guest

    def test_ssh_disconnect_during_poweroff_still_waits_for_guest_shutdown(self):
        for error in (OSError('connection closed'),subprocess.CalledProcessError(255,['ssh']),
                      subprocess.TimeoutExpired('ssh',12)):
            guest=self.guest();guest.ssh.side_effect=error
            with self.subTest(error=type(error).__name__):
                guest.stop()
                guest.process.wait.assert_called_once_with(timeout=20)
                guest.qmp.assert_not_called();guest.process.terminate.assert_not_called();guest.process.kill.assert_not_called()

    def test_clean_poweroff_does_not_force_qemu_exit(self):
        guest=self.guest();guest.stop()
        guest.ssh.assert_called_once();guest.process.wait.assert_called_once_with(timeout=20)
        guest.qmp.assert_not_called();guest.process.terminate.assert_not_called()

    def test_guest_shutdown_timeout_precedes_bounded_qmp_fallback(self):
        guest=self.guest();guest.ssh.side_effect=subprocess.CalledProcessError(255,['ssh'])
        guest.process.wait.side_effect=[subprocess.TimeoutExpired('qemu',20),0]
        calls=Mock();calls.attach_mock(guest.ssh,'ssh');calls.attach_mock(guest.process,'process');calls.attach_mock(guest.qmp,'qmp')
        guest.stop()
        self.assertEqual([c[0] for c in calls.mock_calls],['process.poll','ssh','process.wait','qmp','process.wait'])
        self.assertEqual([c.kwargs['timeout'] for c in guest.process.wait.call_args_list],[20,15])
        guest.qmp.assert_called_once_with('quit');guest.process.kill.assert_not_called()

    def test_live_guest_and_unstarted_or_exited_process_do_not_receive_builder_ssh(self):
        guest=self.guest();guest.is_live_iso=True;guest.stop()
        guest.ssh.assert_not_called();guest.qmp.assert_called_once_with('quit')
        guest=self.guest();guest.process.poll.return_value=0;guest.stop()
        guest.ssh.assert_not_called();guest.qmp.assert_not_called()
        guest=self.guest();guest.process=None;guest.stop();guest.ssh.assert_not_called();guest.qmp.assert_not_called()


if __name__=='__main__':unittest.main()
