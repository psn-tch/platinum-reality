"""Docking/undocking cases from the physical-login failure; no real output changes."""
import importlib.util
import json
import os
import socket
import threading
import time
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('standalone_session', Path(__file__).resolve().parents[1] / 'scripts/standalone-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)


def screen(name, width=1920, height=1080, **extra):
    return dict(name=name, current_mode=dict(width=width, height=height), **extra)


class OutputSelectionTests(unittest.TestCase):
    def test_slow_initial_inventory_gets_startup_budget_without_extending_later_ipc(self):
        # Actual private Unix IPC: the initial response takes longer than the
        # old budget. No real Sway/session/socket or physical outputs are used.
        def reply_after_delay(request):
            with tempfile.TemporaryDirectory(prefix="platinum-ipc-test-") as folder:
                path = Path(folder) / "backend.sock"
                observed, errors = [], []
                with socket.socket(socket.AF_UNIX) as listener:
                    listener.settimeout(5)
                    listener.bind(str(path))
                    listener.listen(1)
                    def serve():
                        try:
                            peer, _ = listener.accept()
                            with peer:
                                peer.settimeout(5)
                                magic, size, kind = session.HEADER.unpack(session.receive_exact(peer, session.HEADER.size))
                                payload = session.receive_exact(peer, size)
                                observed.append((magic, kind, payload))
                                time.sleep(2.25)
                                body = json.dumps([{"name": "Virtual-1", "active": True}]).encode()
                                try:
                                    peer.sendall(session.HEADER.pack(b"i3-ipc", len(body), kind) + body)
                                except (BrokenPipeError, ConnectionResetError):
                                    pass  # Expected after the normal 2 s client times out.
                        except Exception as error:
                            errors.append(error)
                    worker = threading.Thread(target=serve, daemon=True)
                    worker.start()
                    try:
                        return request(path)
                    finally:
                        worker.join(6)
                        self.assertFalse(worker.is_alive(), "Private test IPC server did not stop")
                        self.assertFalse(errors)
                        self.assertEqual(observed, [(b"i3-ipc", 3, b"")])
        with self.assertRaises(TimeoutError):
            reply_after_delay(lambda path: session.ipc(path, 3))
        self.assertEqual(reply_after_delay(session.wait_for_backend_inventory),
                         [{"name": "Virtual-1", "active": True}])
        with self.assertRaises(TimeoutError):
            reply_after_delay(lambda path: session.ipc(path, 3))

    def test_explicit_connected_choice_wins(self):
        selected, reason = session.choose_primary_output([screen('DP-3',3840,2160),screen('eDP-1')], 'eDP-1')
        self.assertEqual((selected['name'],reason),('eDP-1','requested_connector'))

    def test_changed_dock_connector_prefers_external(self):
        selected, reason = session.choose_primary_output([screen('eDP-1',3840,2400),screen('DP-3',2560,1440)], 'DP-2')
        self.assertEqual((selected['name'],reason),('DP-3','fallback_external'))

    def test_auto_chooses_external_when_docked_else_the_built_in_screen(self):
        # The login-screen session asks for "auto" (packaging/platinum-reality-session.in).
        selected, reason = session.choose_primary_output([screen('eDP-1'), screen('HDMI-A-1', 2560, 1440)], 'auto')
        self.assertEqual((selected['name'], reason), ('HDMI-A-1', 'fallback_external'))
        selected, reason = session.choose_primary_output([screen('eDP-1')], 'auto')
        self.assertEqual((selected['name'], reason), ('eDP-1', 'fallback_internal'))

    def test_undocked_laptop_is_usable(self):
        selected, reason = session.choose_primary_output([screen('eDP-1')], 'DP-2')
        self.assertEqual((selected['name'],reason),('eDP-1','fallback_internal'))

    def test_largest_external_is_deterministic(self):
        monitors=[screen('HDMI-A-1'),screen('DP-3',3840,2160),screen('eDP-1',3840,2400)]
        for outputs in (monitors,list(reversed(monitors))):
            self.assertEqual(session.choose_primary_output(outputs,'DP-2')[0]['name'],'DP-3')

    def test_disabled_external_uses_supported_modes(self):
        disabled=dict(name='DP-3',current_mode=None,modes=[dict(width=3840,height=2160)])
        self.assertEqual(session.choose_primary_output([screen('eDP-1'),disabled],'DP-2')[0]['name'],'DP-3')

    def test_non_desktop_and_command_injection_are_not_targets(self):
        outputs=[screen('DP-2',non_desktop=True),screen('DP-3; exit'),screen('eDP-1')]
        self.assertEqual(session.choose_primary_output(outputs,'DP-2')[0]['name'],'eDP-1')

    def test_no_connected_screen_is_an_explicit_error(self):
        with self.assertRaisesRegex(RuntimeError,'No usable desktop output'):
            session.choose_primary_output([], 'DP-2')


class MainDisplayPreferenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.profile = Path(self.temporary.name)
        self.outputs = [screen('DP-3', active=True, make='Test', model='Panel', serial='123'),
                        screen('eDP-1', active=True, make='Laptop', model='Built-in')]
        self.hashes = {'DP-3': 'a' * 32, 'eDP-1': 'b' * 32}
        self.identity = session.main_display_identity(self.outputs[0], self.hashes.get)

    def test_edid_follows_a_renamed_dock_port_not_a_different_monitor_on_the_old_port(self):
        outputs = [dict(self.outputs[0], name='DP-7'), dict(self.outputs[1], name='DP-3')]
        hashes = {'DP-7': 'a' * 32, 'DP-3': 'b' * 32}
        self.assertEqual(session.resolve_main_display(outputs, self.identity, hashes.get), 'DP-7')
        self.assertIsNone(session.resolve_main_display(outputs[1:], self.identity, hashes.get))

    def test_missing_inactive_and_non_desktop_preferences_are_not_targets(self):
        for outputs in ([], [dict(self.outputs[0], active=False)],
                        [dict(self.outputs[0], non_desktop=True)], [self.outputs[1]]):
            with self.subTest(outputs=outputs):
                self.assertIsNone(session.resolve_main_display(outputs, self.identity, self.hashes.get))
        self.assertEqual(session.resolve_main_display(self.outputs, self.identity, self.hashes.get), 'DP-3')

    def test_identical_edids_require_the_original_connector_when_ambiguous(self):
        twins = [dict(self.outputs[0], name='DP-4'), dict(self.outputs[0], name='DP-5')]
        self.assertIsNone(session.resolve_main_display(twins, self.identity, lambda _: 'a' * 32))
        twins[1]['name'] = 'DP-3'
        self.assertEqual(session.resolve_main_display(twins, self.identity, lambda _: 'a' * 32), 'DP-3')

    def test_without_edid_only_a_useful_serial_can_follow_a_port_change(self):
        identity = session.main_display_identity(self.outputs[0], lambda _: None)
        renamed = [dict(self.outputs[0], name='DP-7')]
        self.assertEqual(session.resolve_main_display(renamed, identity, lambda _: None), 'DP-7')
        for serial in ('unknown', '0', ''):
            original = dict(self.outputs[0], serial=serial)
            identity = session.main_display_identity(original, lambda _: None)
            self.assertIsNone(session.resolve_main_display([dict(original, name='DP-7')], identity, lambda _: None))

    def test_connector_fallback_requires_known_metadata_to_agree(self):
        identity = session.main_display_identity(self.outputs[1], lambda _: None)
        self.assertIsNone(session.resolve_main_display([dict(self.outputs[1], model='Different')], identity, lambda _: None))
        self.assertEqual(session.resolve_main_display(self.outputs, identity, lambda _: None), 'eDP-1')
        virtual = {'name': 'WL-1', 'active': True}
        identity = session.main_display_identity(virtual, lambda _: None)
        self.assertEqual(session.resolve_main_display([virtual], identity, lambda _: None), 'WL-1')

    def test_save_is_atomic_private_and_preserves_the_independent_mode_file(self):
        mode = self.profile / 'displays.json'
        mode.write_text('{"version":1,"mode":"extend"}\n')
        session.save_main_display(self.profile, self.identity)
        path = self.profile / 'main-display.json'
        old = path.open('rb')
        self.addCleanup(old.close)
        replacement = session.main_display_identity(self.outputs[1], self.hashes.get)
        session.save_main_display(self.profile, replacement)
        self.assertEqual(json.loads(old.read()), self.identity)
        self.assertEqual(session.saved_main_display(self.profile), replacement)
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(mode.read_text(), '{"version":1,"mode":"extend"}\n')
        self.assertEqual(sorted(p.name for p in self.profile.iterdir()), ['displays.json', 'main-display.json'])

    def test_failed_replace_preserves_previous_bytes_and_removes_temporary_file(self):
        session.save_main_display(self.profile, self.identity)
        before = (self.profile / 'main-display.json').read_bytes()
        with patch.object(session.os, 'replace', side_effect=OSError('test-only failed save')):
            with self.assertRaises(OSError):
                session.save_main_display(self.profile, {'version': 1, 'connector': 'eDP-1'})
        self.assertEqual((self.profile / 'main-display.json').read_bytes(), before)
        self.assertEqual([p.name for p in self.profile.iterdir()], ['main-display.json'])

    def test_settings_symlink_fifo_and_directory_are_not_followed_or_overwritten(self):
        target = self.profile / 'unrelated'
        target.write_text(json.dumps(self.identity))
        path = self.profile / 'main-display.json'
        path.symlink_to(target)
        self.assertIsNone(session.saved_main_display(self.profile))
        with self.assertRaises(OSError):
            session.save_main_display(self.profile, self.identity)
        self.assertEqual(json.loads(target.read_text()), self.identity)
        path.unlink()
        os.mkfifo(path)
        self.assertIsNone(session.saved_main_display(self.profile))
        with self.assertRaises(OSError):
            session.save_main_display(self.profile, self.identity)
        path.unlink()
        path.mkdir()
        self.assertIsNone(session.saved_main_display(self.profile))

    def test_malformed_or_oversized_identity_is_ignored(self):
        path = self.profile / 'main-display.json'
        for data in ([], {}, dict(self.identity, version=True), dict(self.identity, edid='bad'),
                     dict(self.identity, connector='DP-3; exit'), dict(self.identity, model='x' * 257)):
            with self.subTest(data=data):
                path.write_text(json.dumps(data))
                self.assertIsNone(session.saved_main_display(self.profile))
        path.write_bytes(b'x' * 4097)
        self.assertIsNone(session.saved_main_display(self.profile))


class KdeOutputSettingsTests(unittest.TestCase):
    """psn_tch's laptop: a portrait panel KDE shows rotated 90° (left) at 2.3, and two docks."""

    def write(self, sections):
        import json, tempfile
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / 'kwinoutputconfig.json'
        path.write_text(json.dumps(sections))
        return path

    def kwin(self, *entries):
        return [dict(name='outputs', data=list(entries)), dict(name='setups', data=[])]

    def entry(self, name, transform='Normal', scale=1.0, width=1920, height=1080):
        return dict(connectorName=name, transform=transform, scale=scale,
                    mode=dict(width=width, height=height, refreshRate=60000))

    def test_laptop_panel_keeps_its_kde_rotation_and_scale(self):
        path = self.write(self.kwin(self.entry('eDP-1', 'Rotated90', 2.3, 1600, 2560),
                                    self.entry('DP-3', 'Normal', 1.35)))
        preferences = session.kde_output_preferences(path, [screen('eDP-1', 1600, 2560), screen('DP-3')])
        # Confirmed on the real panel: upright with Sway's 270.
        self.assertEqual(preferences['eDP-1'], {'transform': '270', 'scale': 2.3})
        self.assertEqual(preferences['DP-3'], {'transform': 'normal', 'scale': 1.35})

    def test_every_kwin_transform_maps_to_a_sway_transform(self):
        expected = {'Normal': 'normal', 'Rotated90': '270', 'Rotated180': '180', 'Rotated270': '90',
                    'Flipped': 'flipped', 'Flipped90': 'flipped-270', 'Flipped180': 'flipped-180',
                    'Flipped270': 'flipped-90'}
        for kwin, sway in expected.items():
            path = self.write(self.kwin(self.entry('eDP-1', kwin)))
            self.assertEqual(session.kde_output_preferences(path, [screen('eDP-1')])['eDP-1']['transform'], sway)

    def test_a_reused_connector_is_matched_by_the_monitor_mode(self):
        # The 4K monitor and the 1080p monitor have both been on this port.
        path = self.write(self.kwin(self.entry('DP-2', 'Normal', 1.5, 3840, 2160),
                                    self.entry('DP-2', 'Normal', 1.0, 1920, 1080)))
        self.assertEqual(session.kde_output_preferences(path, [screen('DP-2', 3840, 2160)])['DP-2']['scale'], 1.5)
        self.assertEqual(session.kde_output_preferences(path, [screen('DP-2', 1920, 1080)])['DP-2']['scale'], 1.0)
        self.assertNotIn('DP-2', session.kde_output_preferences(path, [screen('DP-2', 2560, 1440)]))

    def test_malformed_unknown_or_unsafe_settings_are_ignored(self):
        path = self.write(self.kwin(self.entry('eDP-1', 'Sideways', 2.0), self.entry('DP-3', 'Normal', 40),
                                    self.entry('DP-4; exit', 'Normal', 1.0), dict(connectorName='HDMI-A-1', scale=True)))
        outputs = [screen('eDP-1'), screen('DP-3'), screen('DP-4; exit'), screen('HDMI-A-1')]
        self.assertEqual(session.kde_output_preferences(path, outputs), {})
        missing = Path(path.parent) / 'absent.json'
        self.assertEqual(session.kde_output_preferences(missing, outputs), {})
        broken = self.write(self.kwin())
        broken.write_text('{not json')
        self.assertEqual(session.kde_output_preferences(broken, outputs), {})

    def test_a_monitor_is_recognised_by_its_edid_on_any_port(self):
        # psn_tch's KDE file: the Samsung was on DP-2 at 1.5, a 1080p APT monitor on DP-3 at 1.35.
        # Today the Samsung is on DP-3: it must keep its own 1.5, not take the APT's 1.35.
        samsung = dict(self.entry('DP-2', 'Normal', 1.5, 3840, 2160), edidHash='364375f4')
        apt = dict(self.entry('DP-3', 'Normal', 1.35, 1920, 1080), edidHash='d87b0493')
        path = self.write(self.kwin(samsung, apt))
        hashes = {'DP-3': '364375f4'}.get
        preferences = session.kde_output_preferences(path, [screen('DP-3', 3840, 2160)], hashes)
        self.assertEqual(preferences['DP-3'], {'transform': 'normal', 'scale': 1.5})

    def test_a_monitor_kde_never_saw_takes_nothing_from_another_on_its_port(self):
        apt = dict(self.entry('DP-3', 'Normal', 1.35, 1920, 1080), edidHash='d87b0493')
        path = self.write(self.kwin(apt))
        self.assertEqual(session.kde_output_preferences(path, [screen('DP-3', 3840, 2160)],
                                                        {'DP-3': '0123abcd'}.get), {})
        # Without an EDID (or with settings that predate it), the port name still matches.
        self.assertEqual(session.kde_output_preferences(path, [screen('DP-3', 1920, 1080)],
                                                        lambda name: None)['DP-3']['scale'], 1.35)

    def test_edid_hash_is_read_only_from_the_named_connector(self):
        import hashlib, tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, data in (('card1-DP-3', b'\x00\xff' * 64), ('card1-DP-30', b'other'), ('card1-HDMI-A-1', b'')):
                (root / name).mkdir()
                (root / name / 'edid').write_bytes(data)
            self.assertEqual(session.sysfs_edid_hash('DP-3', root), hashlib.md5(b'\x00\xff' * 64).hexdigest())
            self.assertIsNone(session.sysfs_edid_hash('HDMI-A-1', root))  # Disconnected: empty.
            self.assertIsNone(session.sysfs_edid_hash('DP-4', root))

    def test_a_symlinked_settings_file_is_not_followed(self):
        path = self.write(self.kwin(self.entry('eDP-1', 'Rotated90', 2.3)))
        link = path.parent / 'linked.json'
        link.symlink_to(path)
        self.assertEqual(session.kde_output_preferences(link, [screen('eDP-1')]), {})


class KdeArrangementTests(unittest.TestCase):
    """psn_tch's saved KDE setups (2026-10-20): with the Samsung the laptop panel is off; with the
    1080p APT monitor the panel sits left and the APT, to its right at 1114, is the main display."""

    HASHES = {'panel': 'a1', 'samsung': 'b2', 'apt': 'c3'}

    def write(self, setups):
        import json, tempfile
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / 'kwinoutputconfig.json'
        outputs = [dict(connectorName='eDP-1', edidHash='a1', transform='Rotated90', scale=2.3),
                   dict(connectorName='DP-2', edidHash='b2', transform='Normal', scale=1.5),
                   dict(connectorName='DP-3', edidHash='c3', transform='Normal', scale=1.35)]
        path.write_text(json.dumps([dict(name='outputs', data=outputs), dict(name='setups', data=setups)]))
        return path

    def place(self, index, enabled, x, priority):
        return dict(outputIndex=index, enabled=enabled, position=dict(x=x, y=0), priority=priority, replicationSource='')

    def owner_setups(self):
        return [dict(lidClosed=False, outputs=[self.place(0, False, -696, 0), self.place(1, True, 0, 1)]),
                dict(lidClosed=True, outputs=[self.place(0, False, 0, 0), self.place(1, True, 696, 1)]),
                dict(lidClosed=True, outputs=[self.place(0, True, 0, 0)]),
                dict(lidClosed=False, outputs=[self.place(0, True, 0, 1)]),
                dict(lidClosed=False, outputs=[self.place(0, True, 0, 2), self.place(2, True, 1114, 1)])]

    def arrangement(self, path, connected, lid):
        hashes = dict(connected)
        return session.kde_arrangement(path, [screen(name) for name in hashes], hashes.get, lid=lid)

    def test_docked_to_the_samsung_on_any_port_the_panel_stays_off(self):
        path = self.write(self.owner_setups())
        docked = [('eDP-1', 'a1'), ('DP-3', 'b2')]  # The Samsung, today on DP-3 (KDE saw it on DP-2).
        self.assertEqual(self.arrangement(path, docked, False),
                         {'eDP-1': dict(enabled=False, x=-696, y=0, primary=False),
                          'DP-3': dict(enabled=True, x=0, y=0, primary=True)})
        self.assertEqual(self.arrangement(path, docked, True)['DP-3']['x'], 696)
        # Both lid states are saved for this pair: without a lid reading it is ambiguous.
        self.assertEqual(self.arrangement(path, docked, None), {})

    def test_the_apt_monitor_extends_the_panel_and_is_the_main_display(self):
        path = self.write(self.owner_setups())
        self.assertEqual(self.arrangement(path, [('eDP-1', 'a1'), ('DP-1', 'c3')], False),
                         {'eDP-1': dict(enabled=True, x=0, y=0, primary=False),
                          'DP-1': dict(enabled=True, x=1114, y=0, primary=True)})
        self.assertEqual(self.arrangement(path, [('eDP-1', 'a1')], False),
                         {'eDP-1': dict(enabled=True, x=0, y=0, primary=True)})

    def test_unknown_combinations_and_malformed_setups_arrange_nothing(self):
        path = self.write(self.owner_setups())
        self.assertEqual(self.arrangement(path, [('eDP-1', 'a1'), ('HDMI-A-1', 'ffff')], False), {})
        self.assertEqual(self.arrangement(path, [('eDP-1', 'a1'), ('DP-3', 'b2'), ('DP-1', 'c3')], False), {})
        broken = self.owner_setups()
        broken[4]['outputs'][1]['position'] = dict(x='1114', y=0)
        self.assertEqual(self.arrangement(self.write(broken), [('eDP-1', 'a1'), ('DP-1', 'c3')], False), {})
        everything_off = [dict(lidClosed=False, outputs=[self.place(0, False, 0, 0)])]
        self.assertEqual(self.arrangement(self.write(everything_off), [('eDP-1', 'a1')], False), {})

    def test_the_lid_switch_is_read_only_when_there_is_exactly_one(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertIsNone(session.lid_closed(root / 'absent'))
            (root / 'LID0').mkdir()
            (root / 'LID0' / 'state').write_text('state:      open\n')
            self.assertIs(session.lid_closed(root), False)
            (root / 'LID0' / 'state').write_text('state:      closed\n')
            self.assertIs(session.lid_closed(root), True)
            (root / 'LID1').mkdir()
            (root / 'LID1' / 'state').write_text('state:      open\n')
            self.assertIsNone(session.lid_closed(root))


class SavedDisplayModeTests(unittest.TestCase):
    """The Monitors panel's "Use all displays" choice, read by the supervisor at login."""

    def setUp(self):
        import tempfile
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.profile = self.root / 'profile'
        self.profile.mkdir()

    def choose(self, contents):
        (self.profile / 'displays.json').write_text(contents)

    def test_only_an_explicit_extend_choice_uses_all_displays(self):
        self.assertEqual(session.saved_display_mode(self.profile), 'single')  # Never chosen.
        self.choose('{"version": 1, "mode": "extend"}')
        self.assertEqual(session.saved_display_mode(self.profile), 'extend')
        self.choose('{"version": 2, "mode": "mirror"}')
        self.assertEqual(session.saved_display_mode(self.profile), 'mirror')
        for contents in ('{"mode": "single"}', '{"mode": "span"}', '["extend"]', '{not json', '{"mode": "extend"' + ' ' * 17000 + '}'):
            self.choose(contents)
            self.assertEqual(session.saved_display_mode(self.profile), 'single', contents[:30])
        target = self.root / 'elsewhere.json'
        target.write_text('{"mode": "extend"}')
        (self.profile / 'displays.json').unlink()
        (self.profile / 'displays.json').symlink_to(target)
        self.assertEqual(session.saved_display_mode(self.profile), 'single')

    def test_the_profile_is_the_launchers(self):
        self.assertEqual(session.desktop_profile('/x/p', {}), Path('/x/p'))
        self.assertEqual(session.desktop_profile(None, {'XDG_DATA_HOME': '/d'}), Path('/d/platinum-reality/profile'))
        self.assertEqual(session.desktop_profile(None, {}), Path.home() / '.local/share/platinum-reality/profile')

    def run_login(self, outcomes):
        """The login's arguments with a fake session run; returns (result, runs)."""
        import argparse, json
        runs = []

        def fake(options):
            runs.append(options)
            outcome = outcomes[len(runs) - 1]
            Path(options.evidence).mkdir()
            (Path(options.evidence) / 'session.json').write_text(json.dumps({
                'was_ready': outcome not in ('early', 'kde'),
                'login_session': 'kde' if outcome == 'kde' else 'platinum'}))
            if outcome in ('early', 'crash-after-ready'):
                raise RuntimeError('Desktop did not map full screen')
            return session.KDE_SESSION_EXIT if outcome == 'kde' else 0

        options = argparse.Namespace(single_output=True, experimental_extended=False, profile=self.profile,
                                     evidence=self.root / 'run', primary_output='DP-2')
        return session.run_with_saved_display_mode(options, fake, {}), runs

    def test_a_login_without_the_choice_is_managed_by_the_monitors_panel(self):
        # Every login runs the display-managed session; its saved choices (one display unless
        # chosen otherwise) decide what is on, and can change while logged in.
        result, runs = self.run_login(['ok'])
        self.assertEqual((result, len(runs), runs[0].single_output, runs[0].experimental_extended,
                          runs[0].display_settings), (0, 1, False, True, True))

    def test_switched_off_monitors_are_read_back_and_bad_entries_dropped(self):
        good = {'version': 1, 'connector': 'eDP-1', 'edid': 'a' * 32, 'make': 'OXP'}
        self.choose(json.dumps({'version': 2, 'mode': 'extend', 'off': [
            good, {'connector': 'DP-1; exit'}, {'connector': 'DP-2', 'edid': 'nothex'}, 'eDP-1', {'connector': 'HDMI-A-1', 'extra': 1}]}))
        settings = session.saved_display_settings(self.profile)
        self.assertEqual(settings['mode'], 'extend')
        self.assertEqual(settings['off'], [good, {'connector': 'HDMI-A-1'}])
        self.choose('{"version": 2, "mode": "extend", "off": "eDP-1"}')
        self.assertEqual(session.saved_display_settings(self.profile), {'mode': 'extend', 'off': [], 'displays': []})

    def test_each_monitors_resolution_size_and_rotation_are_read_back(self):
        panel = {'version': 1, 'connector': 'eDP-1', 'edid': 'a' * 32}
        self.choose(json.dumps({'version': 2, 'mode': 'extend', 'off': [], 'displays': [
            {'identity': panel, 'mode': {'width': 1200, 'height': 1920, 'refresh': 60000}, 'scale': 2.3, 'transform': '270'},
            {'identity': {'connector': 'DP-2'}, 'scale': 9, 'transform': 'sideways', 'mode': {'width': 'x'}},
            {'identity': {'connector': 'DP-3; exit'}, 'scale': 1.5},
            {'identity': {'connector': 'HDMI-A-1'}, 'scale': 1.25, 'transform': 'flipped'},
            {'identity': {'connector': 'DP-4'}, 'position': {'x': -800, 'y': 40}},
            {'identity': {'connector': 'DP-5'}, 'position': {'x': 'left', 'y': 0}}]}))
        self.assertEqual(session.saved_display_settings(self.profile)['displays'], [
            {'identity': panel, 'mode': {'width': 1200, 'height': 1920, 'refresh': 60000}, 'scale': 2.3, 'transform': '270'},
            {'identity': {'connector': 'HDMI-A-1'}, 'scale': 1.25},
            {'identity': {'connector': 'DP-4'}, 'position': {'x': -800, 'y': 40}}])

    def test_control_values_are_strict(self):
        parse = session.parse_display_setting
        self.assertEqual(parse('mode', '1920x1080@60000'), {'width': 1920, 'height': 1080, 'refresh': 60000})
        self.assertEqual((parse('scale', '1.5'), parse('scale', '2'), parse('rotate', '90')), (1.5, 2.0, '90'))
        self.assertEqual((parse('position', '-800,0'), parse('position', '1920,120')),
                         ({'x': -800, 'y': 0}, {'x': 1920, 'y': 120}))
        for kind, text in (('mode', '1920x1080'), ('mode', '1920x1080@60.0'), ('mode', '1x1@1;exit'),
                           ('scale', '0.25'), ('scale', '9'), ('scale', 'nan'), ('scale', '1e1'),
                           ('rotate', 'flipped'), ('rotate', '45'), ('colour', '8'), ('position', '1,2,3'),
                           ('position', '99999,0'), ('position', '1.5,2'), ('position', '0;exit')):
            self.assertIsNone(parse(kind, text), (kind, text))

    def test_modes_are_distinct_and_largest_first(self):
        output = {'current_mode': {'width': 1920, 'height': 1080, 'refresh': 60000, 'picture_aspect_ratio': 'none'},
                  'modes': [{'width': 1280, 'height': 720, 'refresh': 60000}, {'width': 1920, 'height': 1080, 'refresh': 60000},
                            {'width': 1920, 'height': 1080, 'refresh': 60000}, {'width': 1920, 'height': 1080, 'refresh': 75000},
                            {'width': 10, 'height': 10, 'refresh': 60000}, {'width': '1920'}]}
        self.assertEqual(session.display_modes(output), {
            'modes': [{'width': 1920, 'height': 1080, 'refresh': 75000}, {'width': 1920, 'height': 1080, 'refresh': 60000},
                      {'width': 1280, 'height': 720, 'refresh': 60000}],
            'mode': {'width': 1920, 'height': 1080, 'refresh': 60000}})
        self.assertEqual(session.mode_command('DP-2', {'width': 1920, 'height': 1080, 'refresh': 59951}),
                         'output DP-2 mode 1920x1080@59.951Hz')

    def test_saving_keeps_the_mode_an_older_release_reads(self):
        session.save_display_settings(self.profile, 'extend', [{'version': 1, 'connector': 'eDP-1'}])
        data = json.loads((self.profile / 'displays.json').read_text())
        self.assertEqual(data, {'version': 2, 'mode': 'extend', 'off': [{'version': 1, 'connector': 'eDP-1'}], 'displays': []})
        self.assertEqual(session.saved_display_mode(self.profile), 'extend')
        self.assertEqual((self.profile / 'displays.json').stat().st_mode & 0o077, 0)

    def test_the_choice_turns_a_login_into_the_extended_session(self):
        self.choose('{"mode": "extend"}')
        result, runs = self.run_login(['ok'])
        self.assertEqual((result, len(runs), runs[0].single_output, runs[0].experimental_extended), (0, 1, False, True))
        self.assertEqual(runs[0].primary_output, 'DP-2')  # Still the hint for the main display.

    def test_an_extended_start_that_fails_falls_back_to_one_display(self):
        self.choose('{"mode": "extend"}')
        result, runs = self.run_login(['early', 'ok'])
        self.assertEqual((result, len(runs)), (0, 2))
        self.assertTrue(runs[0].experimental_extended)
        self.assertEqual((runs[1].single_output, runs[1].experimental_extended), (True, False))
        self.assertEqual(runs[1].evidence, Path(str(self.root / 'run') + '-one-display'))

    def test_authenticated_kde_choice_is_not_an_extended_startup_failure(self):
        self.choose('{"mode": "extend"}')
        result, runs = self.run_login(['kde', 'ok'])
        self.assertEqual((result, len(runs)), (session.KDE_SESSION_EXIT, 1))

    def test_a_session_that_was_up_is_never_restarted(self):
        # A crash after the desktop was in use is not a failed start: back to the login prompt.
        self.choose('{"mode": "extend"}')
        result, runs = self.run_login(['crash-after-ready'])
        self.assertEqual((result, len(runs)), (1, 1))


class ChosenDisplayTests(unittest.TestCase):
    """Which connected displays the Monitors panel's choices turn on (no output commands)."""
    outputs = [screen('eDP-1', 1200, 1920, active=True), screen('DP-2', make='APT', model='PM1561P', active=True),
               screen('HDMI-A-1', active=False), screen('VR-1', non_desktop=True)]

    @staticmethod
    def edid(name):
        return {'eDP-1': '1' * 32, 'DP-2': '2' * 32, 'HDMI-A-1': '3' * 32}.get(name)

    def choose(self, mode, off, main='DP-2', lid=None):
        identities = [{'version': 1, 'connector': n, 'edid': self.edid(n)} for n in off]
        return session.chosen_display_names(self.outputs, lid, mode, identities, main, self.edid)

    def test_one_display_is_only_the_main_one(self):
        self.assertEqual(self.choose('single', []), ['DP-2'])
        self.assertEqual(self.choose('single', [], main='eDP-1'), ['eDP-1'])
        self.assertEqual(self.choose('single', [], main='gone'), ['DP-2'])  # Usual choice: the external one.

    def test_all_displays_except_the_switched_off_ones(self):
        self.assertEqual(self.choose('extend', []), ['DP-2', 'HDMI-A-1', 'eDP-1'])
        self.assertEqual(self.choose('extend', ['eDP-1']), ['DP-2', 'HDMI-A-1'])
        self.assertEqual(self.choose('extend', ['eDP-1', 'HDMI-A-1']), ['DP-2'])

    def test_a_switched_off_monitor_is_recognised_on_another_port(self):
        identities = [{'version': 1, 'connector': 'DP-1', 'edid': '2' * 32}]  # The APT, docked elsewhere before.
        self.assertEqual(session.chosen_display_names(self.outputs, None, 'extend', identities, 'eDP-1', self.edid),
                         ['HDMI-A-1', 'eDP-1'])

    def test_never_every_display_off(self):
        self.assertEqual(self.choose('extend', ['eDP-1', 'DP-2', 'HDMI-A-1']), ['DP-2'])
        self.assertEqual(self.choose('extend', ['eDP-1', 'DP-2', 'HDMI-A-1'], main='eDP-1'), ['eDP-1'])

    def test_a_shut_lid_keeps_the_built_in_panel_off(self):
        self.assertEqual(self.choose('extend', [], lid=True), ['DP-2', 'HDMI-A-1'])
        self.assertEqual(self.choose('single', [], main='eDP-1', lid=True), ['DP-2'])

    def test_labels_name_the_monitor_not_the_port(self):
        self.assertEqual(session.display_label(screen('DP-2', make='APT', model='PM1561P')), 'PM1561P')
        self.assertEqual(session.display_label(screen('DP-3', make='Samsung', model='Unknown')), 'Samsung')
        self.assertEqual(session.display_label(screen('DP-4')), 'DP-4')


class MirrorPlanTests(unittest.TestCase):
    """Every other display shows the main one, scaled to fit and centred over it."""

    @staticmethod
    def output(name, x, y, width, height, mode, transform='normal', scale=1.0):
        return {'name': name, 'active': True, 'scale': scale, 'transform': transform,
                'rect': {'x': x, 'y': y, 'width': width, 'height': height},
                'current_mode': {'width': mode[0], 'height': mode[1], 'refresh': 60000}}

    def test_a_same_shape_display_shows_exactly_the_main_display(self):
        outputs = [self.output('DP-2', 0, 0, 1280, 720, (1920, 1080), scale=1.5),
                   self.output('HDMI-A-1', 1280, 0, 1920, 1080, (1920, 1080))]
        self.assertEqual(session.mirror_plan(outputs, 'DP-2'), {'HDMI-A-1': {'x': 0, 'y': 0, 'scale': 1.5}})

    def test_a_taller_rotated_panel_shows_bars_above_and_below(self):
        # The external 16:9 display is main; the rotated 16:10 built-in panel fits its width.
        outputs = [self.output('DP-2', 0, 0, 1280, 720, (1920, 1080), scale=1.5),
                   self.output('eDP-1', 1280, 0, 522, 835, (1200, 1920), transform='270', scale=2.3)]
        plan = session.mirror_plan(outputs, 'DP-2')['eDP-1']
        self.assertEqual(plan['scale'], 1.5)  # 1920 wide / 1280.
        self.assertEqual((plan['x'], plan['y']), (0, -40))  # 1920x1200 / 1.5 = 1280x800, centred.

    def test_the_fit_never_crops_and_stays_in_wayland_scale_steps(self):
        outputs = [self.output('DP-1', 0, 0, 1000, 700, (1000, 700)),
                   self.output('DP-3', 1000, 0, 1366, 768, (1366, 768))]
        plan = session.mirror_plan(outputs, 'DP-1')['DP-3']
        self.assertEqual(plan['scale'] * 120, int(plan['scale'] * 120))
        self.assertGreaterEqual(round(1366 / plan['scale']), 1000)
        self.assertGreaterEqual(round(768 / plan['scale']), 700)
        self.assertLessEqual(plan['x'], 0)
        self.assertLessEqual(plan['y'], 0)

    def test_a_main_display_that_is_off_or_unusable_is_refused(self):
        outputs = [self.output('DP-1', 0, 0, 1000, 700, (1000, 700))]
        with self.assertRaises(ValueError):
            session.mirror_plan(outputs, 'DP-9')
        outputs.append({**self.output('DP-2', 1000, 0, 800, 600, (800, 600)), 'current_mode': {}})
        with self.assertRaises(ValueError):
            session.mirror_plan(outputs, 'DP-1')

    def test_mirror_is_a_saved_choice_that_keeps_its_switched_off_list(self):
        import tempfile
        with tempfile.TemporaryDirectory() as profile:
            session.save_display_settings(profile, 'mirror', [{'version': 1, 'connector': 'eDP-1'}])
            self.assertEqual(session.saved_display_settings(profile)['mode'], 'mirror')
            self.assertEqual(session.saved_display_mode(profile), 'mirror')


class PanelDefaultTests(unittest.TestCase):
    def test_owners_panel_defaults_to_their_exact_rotation_and_scale(self):
        panel = screen('eDP-1', 1600, 2560, make='HSX', model='YHB02P24')
        self.assertEqual(session.panel_defaults([panel, screen('DP-2', make='Dell', model='U2723QE')]),
                         {'eDP-1': {'transform': '270', 'scale': 2.3}})

    def test_another_laptops_panel_is_never_turned(self):
        self.assertEqual(session.panel_defaults([screen('eDP-1', make='BOE', model='NE135FBM')]), {})
        self.assertEqual(session.panel_defaults([screen('DP-1', make='HSX', model='YHB02P24')]), {})
        self.assertEqual(session.panel_defaults([screen('eDP-1')]), {})


class BackendConfigTests(unittest.TestCase):
    def test_touch_input_settings_and_sway_accepts_the_config(self):
        import shutil, subprocess, tempfile
        self.assertIn('input type:touchpad {\n    tap enabled\n    scroll_method two_finger\n}', session.CONFIG)
        self.assertIn('input type:touch {\n    map_to_output eDP-1\n}', session.CONFIG)
        if not shutil.which('sway'):
            self.skipTest('sway is not installed')
        import os
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'sway.conf'
            config.write_text(session.CONFIG)
            # Validation still creates a backend: force headless so no seat or device is touched.
            environment = {k: v for k, v in os.environ.items()
                           if k not in ('WAYLAND_DISPLAY', 'DISPLAY', 'SWAYSOCK', 'XDG_SESSION_ID')}
            environment.update(WLR_BACKENDS='headless', WLR_LIBINPUT_NO_DEVICES='1', XDG_RUNTIME_DIR=directory)
            result = subprocess.run(['sway', '--validate', '--config', str(config)], env=environment,
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class PersonalCursorTests(unittest.TestCase):
    def test_imported_cursor_theme_reaches_every_client_and_keeps_its_parent(self):
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            environment = {'HOME': home}
            self.assertFalse(session.personal_cursors(environment))
            self.assertEqual(environment, {'HOME': home})
            theme = Path(home) / '.local/share/platinum-reality/assets/cursors/Platinum-MacOS9'
            theme.mkdir(parents=True)
            (theme / 'index.theme').write_text('[Icon Theme]\nInherits=capitaine-cursors\n')
            self.assertTrue(session.personal_cursors(environment))
            self.assertEqual(environment['XCURSOR_THEME'], 'Platinum-MacOS9')
            search = environment['XCURSOR_PATH'].split(':')
            self.assertEqual(search[0], str(theme.parent))
            self.assertIn('/usr/share/icons', search)
            environment = {'HOME': home, 'XCURSOR_PATH': '/opt/icons'}
            session.personal_cursors(environment)
            self.assertEqual(environment['XCURSOR_PATH'], str(theme.parent) + ':/opt/icons')

    def test_startup_chime_plays_the_users_own_sound_once_per_boot(self):
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            boot = home / 'boot_id'
            boot.write_text('first-boot\n')
            played = home / 'played'
            player = home / 'player'
            player.write_text(f'#!/bin/sh\necho "$1 $2" >> {played}\n')
            player.chmod(0o755)
            environment = {'HOME': str(home), 'PATH': '/usr/bin:/bin'}
            chime = lambda: session.startup_chime(environment, boot_id=boot, players=(str(home / 'absent'), str(player)),
                                                  wpctl=str(home / 'no-wpctl'))
            # No sound imported: nothing plays, nothing is recorded.
            self.assertIsNone(chime())
            self.assertFalse((home / '.local/state/platinum-reality/startup-chime-boot').exists())
            assets = home / '.local/share/platinum-reality/assets'
            assets.mkdir(parents=True)
            (assets / 'startup-chime.aiff').write_bytes(b'FORM')
            process = chime()
            self.assertIsNotNone(process)
            process.wait(5)
            # At half volume (pw-play's factor; paplay would get 32768 of 65536).
            self.assertEqual(played.read_text().strip(), '--volume=0.5 ' + str(assets / 'startup-chime.aiff'))
            self.assertEqual(session.chime_volume_option('/usr/bin/paplay'), '--volume=32768')
            # Once per boot: logging in again (after Log Out) is silent; the next boot chimes.
            self.assertIsNone(chime())
            boot.write_text('second-boot\n')
            chime().wait(5)
            self.assertEqual(len(played.read_text().splitlines()), 2)
            # A linked sound is not played; WAV is preferred when both exist.
            (assets / 'startup-chime.wav').symlink_to(assets / 'startup-chime.aiff')
            boot.write_text('third-boot\n')
            chime().wait(5)
            self.assertEqual(played.read_text().splitlines()[-1], '--volume=0.5 ' + str(assets / 'startup-chime.aiff'))

    def test_startup_chime_follows_the_sound_panel_setting(self):
        # Sound ▸ Startup (psn_tch, 2026-10-29): off means no chime; the volume slider sets its level.
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            boot = home / 'boot_id'
            played = home / 'played'
            player = home / 'player'
            player.write_text(f'#!/bin/sh\necho "$1 $2" >> {played}\n')
            player.chmod(0o755)
            assets = home / '.local/share/platinum-reality/assets'
            assets.mkdir(parents=True)
            (assets / 'startup-chime.wav').write_bytes(b'RIFF')
            profile = home / '.local/share/platinum-reality/profile'
            profile.mkdir()
            settings = profile / 'startup-sound.json'
            environment = {'HOME': str(home), 'PATH': '/usr/bin:/bin'}

            def chime(name, **extra):
                boot.write_text(name + '\n')
                return session.startup_chime(environment, boot_id=boot, players=(str(player),),
                                             wpctl=str(home / 'no-wpctl'), **extra)
            settings.write_text(json.dumps({'version': 1, 'enabled': False, 'volume': 80}))
            self.assertIsNone(chime('off'))
            # Turning it back on later the same boot still chimes at the next Log In of that boot.
            settings.write_text(json.dumps({'version': 1, 'enabled': True, 'volume': 80}))
            chime('off').wait(5)
            self.assertEqual(played.read_text().splitlines(), ['--volume=0.8 ' + str(assets / 'startup-chime.wav')])
            self.assertEqual(session.chime_volume_option('/usr/bin/paplay', 0.8), '--volume=52428')
            settings.write_text(json.dumps({'version': 1, 'enabled': True, 'volume': 0}))
            self.assertIsNone(chime('silent'))
            # A profile named on the command line is the one read.
            other = home / 'other-profile'
            other.mkdir()
            (other / 'startup-sound.json').write_text(json.dumps({'version': 1, 'enabled': True, 'volume': 100}))
            chime('other', profile=other).wait(5)
            self.assertEqual(played.read_text().splitlines()[-1], '--volume=1 ' + str(assets / 'startup-chime.wav'))
            # Malformed, out-of-range or linked settings keep the defaults: on, at half volume.
            for content in ('not json', json.dumps({'enabled': 'no', 'volume': 400}), json.dumps([1, 2])):
                settings.write_text(content)
                self.assertEqual(session.startup_sound_settings(profile), {'enabled': True, 'volume': 0.5})
            settings.unlink()
            elsewhere = home / 'elsewhere.json'
            elsewhere.write_text(json.dumps({'version': 1, 'enabled': False, 'volume': 10}))
            settings.symlink_to(elsewhere)
            self.assertEqual(session.startup_sound_settings(profile), {'enabled': True, 'volume': 0.5})
            chime('default').wait(5)
            self.assertEqual(played.read_text().splitlines()[-1], '--volume=0.5 ' + str(assets / 'startup-chime.wav'))

    def test_startup_chime_waits_for_an_audio_output(self):
        import tempfile, time
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            boot = home / 'boot_id'
            boot.write_text('boot\n')
            log = home / 'log'
            # The audio output appears on the third look, as USB speakers do at the first Log In.
            wpctl = home / 'wpctl'
            wpctl.write_text(f'#!/bin/sh\necho look >> {log}\n[ "$(grep -c look {log})" -ge 3 ]\n')
            wpctl.chmod(0o755)
            player = home / 'player'
            player.write_text(f'#!/bin/sh\necho "play $2" >> {log}\n')
            player.chmod(0o755)
            assets = home / '.local/share/platinum-reality/assets'
            assets.mkdir(parents=True)
            (assets / 'startup-chime.wav').write_bytes(b'RIFF')
            started = time.monotonic()
            process = session.startup_chime({'HOME': str(home), 'PATH': '/usr/bin:/bin'}, boot_id=boot,
                                            players=(str(player),), wpctl=str(wpctl))
            # The supervisor is not held up while it waits.
            self.assertLess(time.monotonic() - started, 0.3)
            process.wait(10)
            self.assertEqual(log.read_text().splitlines(), ['look', 'look', 'look', f'play {assets / "startup-chime.wav"}'])

    def test_a_linked_theme_is_not_used(self):
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            folder = Path(home) / '.local/share/platinum-reality/assets/cursors'
            (Path(home) / 'elsewhere').mkdir()
            (Path(home) / 'elsewhere/index.theme').write_text('[Icon Theme]\n')
            folder.mkdir(parents=True)
            (folder / 'Platinum-MacOS9').symlink_to(Path(home) / 'elsewhere')
            environment = {'HOME': home}
            self.assertFalse(session.personal_cursors(environment))
            self.assertNotIn('XCURSOR_THEME', environment)


class IdleCommandTests(unittest.TestCase):
    def test_recovery_shortcut_is_available_while_the_session_is_locked(self):
        self.assertIn('bindsym --locked --inhibited --no-repeat Ctrl+Alt+l nop platinum-lock', session.CONFIG)

    def test_swayidle_never_reads_the_users_own_config(self):
        # A personal "before-sleep swaylock" ran first and blocked this lock (2026-10-22).
        command = session.idle_command('LOCK', 'RESUME')
        self.assertEqual(command[:4], ['swayidle', '-C', '/dev/null', '-w'])
        self.assertEqual(command[4:], ['before-sleep', 'LOCK', 'after-resume', 'RESUME', 'lock', 'LOCK'])


if __name__ == '__main__':
    unittest.main()
