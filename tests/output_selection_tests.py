"""Docking/undocking cases from the physical-login failure; no real output changes."""
import importlib.util
from pathlib import Path
import sys
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('standalone_session', Path(__file__).resolve().parents[1] / 'scripts/standalone-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)


def screen(name, width=1920, height=1080, **extra):
    return dict(name=name, current_mode=dict(width=width, height=height), **extra)


class OutputSelectionTests(unittest.TestCase):
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
        for contents in ('{"mode": "single"}', '{"mode": "mirror"}', '["extend"]', '{not json', '{"mode": "extend"' + ' ' * 5000 + '}'):
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
            (Path(options.evidence) / 'session.json').write_text(json.dumps({'was_ready': outcome != 'early'}))
            if outcome in ('early', 'crash-after-ready'):
                raise RuntimeError('Desktop did not map full screen')
            return 0

        options = argparse.Namespace(single_output=True, experimental_extended=False, profile=self.profile,
                                     evidence=self.root / 'run', primary_output='DP-2')
        return session.run_with_saved_display_mode(options, fake, {}), runs

    def test_a_login_without_the_choice_is_one_display(self):
        result, runs = self.run_login(['ok'])
        self.assertEqual((result, len(runs), runs[0].single_output, runs[0].experimental_extended), (0, 1, True, False))

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

    def test_a_session_that_was_up_is_never_restarted(self):
        # A crash after the desktop was in use is not a failed start: back to the login prompt.
        self.choose('{"mode": "extend"}')
        result, runs = self.run_login(['crash-after-ready'])
        self.assertEqual((result, len(runs)), (1, 1))


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
    def test_swayidle_never_reads_the_users_own_config(self):
        # A personal "before-sleep swaylock" ran first and blocked this lock (2026-10-22).
        command = session.idle_command('LOCK')
        self.assertEqual(command[:4], ['swayidle', '-C', '/dev/null', '-w'])
        self.assertEqual(command[4:], ['before-sleep', 'LOCK', 'after-resume', 'LOCK', 'lock', 'LOCK'])


if __name__ == '__main__':
    unittest.main()
