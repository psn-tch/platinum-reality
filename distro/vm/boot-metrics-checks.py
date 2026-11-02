#!/usr/bin/env python3
"""Synthetic command results only. No real systemd queries, services or VM."""
import importlib.util
from pathlib import Path
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('metrics',Path(__file__).with_name('boot-metrics.py'))
metrics=importlib.util.module_from_spec(spec);spec.loader.exec_module(metrics)


def result(text='',status=0,truncated=False):
    return {'stdout':text,'stderr':'','status':status,'truncated':truncated}


class MetricsChecks(unittest.TestCase):
    def test_success_preserves_raw_outputs_and_only_ten_blame_lines(self):
        calls=[]
        def run(verb):
            calls.append(verb)
            return result('\n'.join(f'{20-i}s unit-{i}.service' for i in range(12)) if verb=='blame' else 'fixed '+verb)
        record=metrics.collect(run=run,now=lambda:5,sleep=Mock())
        self.assertTrue(record['ok']);self.assertTrue(record['startup_complete'])
        self.assertEqual(len(record['top_10_blame']),10)
        self.assertEqual(record['top_10_blame'][0],'20s unit-0.service')
        self.assertEqual(calls,['time','blame','critical-chain','--version'])
        self.assertIn('not a causal',record['scope'])
        self.assertEqual(record['version_command']['stdout'],'fixed --version')

    def test_unfinished_startup_retries_then_reports_actual_success(self):
        count=0;clock=[0]
        def run(verb):
            nonlocal count
            if verb=='time':
                count+=1
                return result('startup finished' if count==3 else 'not finished',0 if count==3 else 1)
            return result(verb)
        record=metrics.collect(run=run,now=lambda:clock[0],sleep=lambda n:clock.__setitem__(0,clock[0]+n))
        self.assertTrue(record['ok']);self.assertEqual(record['timing_attempts'],3)
        self.assertEqual(record['time']['stdout'],'startup finished')

    def test_deadline_does_not_manufacture_a_boot_time(self):
        clock=[0]
        def run(verb):
            if verb=='time':clock[0]+=10;return result('not finished',1)
            return result(verb)
        record=metrics.collect(run=run,now=lambda:clock[0],sleep=lambda n:clock.__setitem__(0,clock[0]+n))
        self.assertFalse(record['ok']);self.assertFalse(record['startup_complete'])
        self.assertEqual(record['time']['status'],1);self.assertLessEqual(record['timing_attempts'],4)

    def test_partial_or_truncated_results_cannot_pass(self):
        for broken in ('blame','critical-chain','--version'):
            for trunc in (True,False):
                def run(verb):return result(verb,0 if trunc or verb!=broken else 1,trunc and verb==broken)
                record=metrics.collect(run=run,now=lambda:0,sleep=Mock())
                self.assertFalse(record['ok'])

    def test_host_role_refuses_before_any_virtualization_or_system_manager_query(self):
        with patch.object(metrics.socket,'gethostname',return_value='ordinary-host'), \
                patch.object(metrics.subprocess,'run') as command:
            with self.assertRaises(SystemExit):metrics.require_role('installed')
            command.assert_not_called()

    def test_role_is_not_enough_without_qemu_and_no_container(self):
        for responses in ([SimpleNamespace(returncode=1,stdout='none')],
                          [SimpleNamespace(returncode=0,stdout='kvm'),SimpleNamespace(returncode=0)]):
            with patch.object(metrics.socket,'gethostname',return_value='pr-installed'), \
                    patch.object(metrics.subprocess,'run',side_effect=responses) as commands:
                with self.assertRaises(SystemExit):metrics.require_role('installed')
                self.assertTrue(all(call.args[0][0]=='/usr/bin/systemd-detect-virt' for call in commands.call_args_list))

    def test_command_timeout_and_output_limits_are_explicit(self):
        with patch.object(metrics.subprocess,'run',side_effect=subprocess.TimeoutExpired('fixed metrics',10)):
            self.assertEqual(metrics.command('time')['status'],124)
        with patch.object(metrics.subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout='x'*(metrics.LIMIT+1),stderr='e'*5000)) as commands:
            record=metrics.command('blame')
            self.assertTrue(record['truncated']);self.assertEqual(len(record['stdout']),metrics.LIMIT)
            self.assertEqual(len(record['stderr']),4096)
            self.assertEqual(commands.call_args.args[0],['/usr/bin/systemd-analyze','--no-pager','blame'])

    def test_missing_tool_is_an_explicit_failed_observation(self):
        with patch.object(metrics.subprocess,'run',side_effect=FileNotFoundError('missing tool')):
            record=metrics.command('time')
        self.assertEqual(record['status'],127)
        self.assertIn('missing tool',record['stderr'])
        self.assertEqual(record['stdout'],'')


if __name__=='__main__':unittest.main()
