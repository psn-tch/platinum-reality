#!/usr/bin/env python3
"""Synthetic reboot-controller refusals; no VM, credentials or power actions."""
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('proof', Path(__file__).with_name('install-proof.py'))
proof = importlib.util.module_from_spec(spec); spec.loader.exec_module(proof)


def point(x, y, left=None):
    return {'x': x, 'y': y, 'left': x if left is None else left}


def fixture():
    gui = Mock(); gui.capture.return_value = []
    gui.matches.side_effect = [[point(280, 13, 260)], [point(290, 120)],
                               [point(300, 142)], [point(310, 164)]]
    receipt = {'encrypted': True, 'greeter': 'platinum', 'unlock_ui': 'plymouth',
               'gpu': 'virtio-vga', 'requested_output_capacity': 1}
    return gui, receipt


class RepeatLoginChecks(unittest.TestCase):
    def test_bad_capture_geometry_cannot_send_any_pointer_or_password(self):
        gui, receipt = fixture(); gui.capture.side_effect = ValueError('Unknown geometry')
        with patch.object(proof, 'observe_installed_boot') as login:
            with self.assertRaisesRegex(ValueError, 'geometry'):
                proof.restart_and_repeat_login(Mock(), 'synthetic-only', receipt, gui)
            gui.click.assert_not_called(); login.assert_not_called()
        self.assertNotIn('reboot_login', receipt)

    def test_missing_or_duplicate_special_menu_does_not_request_restart(self):
        for matches in ([], [point(280, 13), point(300, 13)], [point(280, 50)]):
            with self.subTest(matches=matches):
                gui, receipt = fixture(); gui.matches.side_effect = [matches]
                with patch.object(proof, 'observe_installed_boot') as login:
                    with self.assertRaisesRegex(ValueError, 'Special menu'):
                        proof.restart_and_repeat_login(Mock(), 'synthetic-only', receipt, gui)
                    self.assertEqual(gui.click.call_count, 1); login.assert_not_called()
                self.assertNotIn('reboot_login', receipt)

    def test_wrong_ambiguous_or_outside_power_menu_does_not_send_final_click(self):
        menus = [([], [point(300, 142)], [point(310, 164)]),
                 ([point(290, 120)], [point(300, 142), point(300, 160)], [point(310, 184)]),
                 ([point(290, 142)], [point(300, 120)], [point(310, 164)]),
                 ([point(290, 120)], [point(800, 142)], [point(310, 164)])]
        for menu in menus:
            with self.subTest(menu=menu):
                gui, receipt = fixture(); gui.matches.side_effect = [[point(280, 13, 260)], *menu]
                with patch.object(proof, 'observe_installed_boot') as login:
                    with self.assertRaisesRegex(ValueError, 'power menu'):
                        proof.restart_and_repeat_login(Mock(), 'synthetic-only', receipt, gui)
                    self.assertEqual(gui.click.call_count, 2); login.assert_not_called()
                self.assertNotIn('reboot_login', receipt)

    def test_real_menu_request_reuses_recognizers_without_reset_or_target_changes(self):
        gui, receipt = fixture(); guest = Mock()
        def observe(actual_guest, password, repeated, stem):
            self.assertIs(actual_guest, guest); self.assertEqual(password, 'synthetic-only')
            self.assertEqual(stem, 'reboot-'); self.assertNotIn('password_login', repeated)
            repeated['password_login'] = 'PASS'
        with patch.object(proof, 'observe_installed_boot', side_effect=observe):
            proof.restart_and_repeat_login(guest, 'synthetic-only', receipt, gui)
        self.assertEqual([call.args[1:] for call in gui.click.call_args_list],
                         [(600, 740), (280, 13), (300, 142)])
        self.assertTrue(receipt['reboot_login']['ok'])
        self.assertNotIn('synthetic-only', repr(receipt))
        guest.qmp.assert_not_called(); guest.start.assert_not_called(); guest.stop.assert_not_called()
        guest.ssh.assert_not_called()

    def test_missing_second_unlock_or_login_propagates_failure_without_success(self):
        gui, receipt = fixture()
        with patch.object(proof, 'observe_installed_boot', side_effect=TimeoutError('No fresh unlock')):
            with self.assertRaises(TimeoutError):
                proof.restart_and_repeat_login(Mock(), 'synthetic-only', receipt, gui)
        self.assertNotIn('ok', receipt['reboot_login'])

    def test_capture_prefix_cannot_escape_the_owned_artifact_directory(self):
        guest = Mock()
        with self.assertRaises(ValueError):
            proof.observe_installed_boot(guest, 'synthetic-only', {}, stem='../outside-')
        guest.qmp.assert_not_called()

    def test_flag_requires_encrypted_gui_proof_before_starting_any_vm(self):
        for args in (['--repeat-login'], ['--repeat-login', '--image-engine', '--boot-proof'],
                     ['--repeat-login', '--image-engine', '--boot-proof', '--gui', '--unencrypted']):
            with self.subTest(args=args), patch.object(proof, 'Guest') as guest, \
                    patch('sys.stderr', new_callable=io.StringIO):
                with self.assertRaises(SystemExit) as result:
                    proof.main(['/unused.iso', *args])
                self.assertEqual(result.exception.code, 2); guest.assert_not_called()


if __name__ == '__main__':
    unittest.main()
