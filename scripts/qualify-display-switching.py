#!/usr/bin/env python3
"""Monitors panel On boxes on an owned nested Sway (two outputs), never a physical seat.

A managed session (--display-settings, as every login now runs) starts with only the main
display, turns the other one on and keeps it, lets a change lapse so the supervisor reverts it,
saves a switched-off monitor, refuses to turn off the last display and honours the saved choice
at the next login.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import socket
from pathlib import Path
import subprocess
import tempfile
import time

os.environ["PLATINUM_DISABLE_HOST_HANDHELD"] = "1"

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--launcher', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
options = parser.parse_args()
project = Path(__file__).resolve().parents[1]
owned = os.environ.get('FRANKENSTEIN_HEADLESS_IPC', '')
if not owned.startswith('/tmp/platinum-headless.') or not Path(owned).is_socket():
    parser.error('Use with-headless-display.py --two-outputs; an owned headless venue is required')
launcher = options.launcher.resolve(strict=True)
if not launcher.is_relative_to(project / 'build'):
    parser.error('Use a separate stage inside this project build directory')
evidence = options.evidence.absolute()
evidence.mkdir(parents=True, exist_ok=False, mode=0o700)
spec = importlib.util.spec_from_file_location('session', project / 'scripts/standalone-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)
checks = {}
record = {'hardware_qualified': False, 'checks': checks, 'snapshots': [],
          'source_sha256': {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in (Path(__file__), project / 'scripts/standalone-session.py',
                                      project / 'src/desktop/main.cpp', project / 'src/desktop/manager.cpp',
                                      project / 'src/desktop/display_topology_feed.cpp')}}


def read(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {}


def wait(predicate, timeout=20):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        value = predicate()
        if value:
            return value
        if child.poll() is not None:
            raise AssertionError('Owned session exited; inspect the supervisor log and session/desktop.txt')
        time.sleep(0.05)
    raise AssertionError('Owned session condition timed out; inspect evidence')


def control(command):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(8)
        peer.connect(state['control'])
        peer.sendall(command.encode() + b'\n')
        return json.loads(peer.recv(16384))


def active():
    return sorted(o['name'] for o in session.ipc(Path(state['backend_ipc']), 3) if o.get('active'))


def topology():
    """The published snapshot once the desktop has applied it."""
    current = read(state_file)
    snapshot = read(current.get('display_topology', '/nonexistent'))
    if snapshot and current.get('display_applied_generation') == snapshot.get('generation'):
        return snapshot
    return None


def settled(on, pending):
    """Wait for the desktop to apply a layout with exactly these displays on."""
    def ready():
        snapshot = topology()
        if (snapshot and sorted(o['name'] for o in snapshot['outputs']) == on
                and snapshot.get('pending') is pending and active() == on):
            return snapshot
        return None
    snapshot = wait(ready)
    record['snapshots'].append(snapshot)
    return snapshot


def start(label):
    global child, state, state_file
    state_file = evidence / label / 'session.json'
    arguments = ['python3', '-B', str(project / 'scripts/standalone-session.py'), '--nested-check',
                 '--experimental-extended', '--display-settings', '--primary-output', 'WL-2',
                 '--launcher', str(launcher), '--evidence', str(evidence / label),
                 '--workspace', str(fixture / 'Workspace'), '--profile', str(profile),
                 '--shell-argument=--state-file', '--shell-argument=' + str(evidence / (label + '-desktop.json'))]
    log = (evidence / (label + '-supervisor.txt')).open('wb')
    child = subprocess.Popen(arguments, env=environment, stdout=log, stderr=subprocess.STDOUT)
    state = wait(lambda: s if (s := read(state_file)).get('ready') else None)


def stop():
    assert control('logout')['ok'] and child.wait(timeout=20) == 0
    assert read(state_file)['runtime_removed']


with tempfile.TemporaryDirectory(prefix='platinum-switching-check-') as temporary:
    fixture = Path(temporary)
    profile = fixture / 'Profile'
    environment = os.environ.copy()
    environment.update(WLR_WL_OUTPUTS='2', HOME=str(fixture / 'home'),
                       XDG_CONFIG_HOME=str(fixture / 'config'), XDG_DATA_HOME=str(fixture / 'data'),
                       XDG_CACHE_HOME=str(fixture / 'cache'), XDG_STATE_HOME=str(fixture / 'state'))
    (fixture / 'home').mkdir()
    settings = profile / 'displays.json'
    child = None
    try:
        start('first-login')
        snapshot = settled(['WL-2'], False)
        assert [d['name'] for d in snapshot['available']] == ['WL-1'] and set(snapshot['labels']) == {'WL-2'}
        assert not settings.exists()
        checks['without_a_choice_only_the_main_display_is_on_and_the_other_is_listed_off'] = True

        answer = control('display WL-1 on')
        assert answer['ok'] and answer['display_change'] == 'waiting', answer
        settled(['WL-1', 'WL-2'], True)
        assert not settings.exists()  # Nothing is saved before Keep.
        assert not control('display WL-2 off')['ok']  # One change at a time.
        assert control('display keep')['ok']
        snapshot = settled(['WL-1', 'WL-2'], False)
        assert snapshot['available'] == [] and snapshot['primary'] == 'WL-2'
        assert read(settings) == {'version': 2, 'mode': 'extend', 'off': [], 'displays': []}
        checks['turning_a_display_on_applies_at_once_and_keep_saves_it'] = True

        def rects():
            return {o['name']: (o['rect']['x'], o['rect']['y']) for o in session.ipc(Path(state['backend_ipc']), 3)
                    if o.get('active')}

        def arranged(expected, pending):
            return wait(lambda: t if (t := topology()) and t.get('pending') is pending and rects() == expected else None)

        start_rects = rects()
        wl2 = next(o for o in session.ipc(Path(state['backend_ipc']), 3) if o['name'] == 'WL-2')['rect']
        right = (wl2['x'] + wl2['width'], wl2['y'])
        answer = control(f'display WL-1 position {right[0]},{right[1]}')
        assert answer['ok'], answer
        moved = rects()
        assert moved['WL-1'][0] > moved['WL-2'][0] and moved['WL-1'][1] == moved['WL-2'][1], moved
        assert min(x for x, _ in moved.values()) == 0
        record['snapshots'].append(arranged(moved, True))
        assert control('display keep')['ok']
        arranged(moved, False)
        saved_positions = {d['identity']['connector']: (d['position']['x'], d['position']['y'])
                           for d in read(settings)['displays'] if 'position' in d}
        assert saved_positions == moved, (saved_positions, moved)
        checks['dragging_a_display_beside_another_applies_and_keep_saves_the_arrangement'] = True

        assert control(f"display WL-1 position -{moved['WL-1'][0]},0")['ok']
        assert rects() != moved
        assert control('display revert')['ok']
        arranged(moved, False)
        assert {d['identity']['connector']: (d['position']['x'], d['position']['y'])
                for d in read(settings)['displays'] if 'position' in d} == moved
        for refused in ('display WL-1 position 0,0', 'display WL-1 position 1,2,3', 'display WL-9 position 0,0'):
            assert not control(refused)['ok'], refused  # Overlapping, malformed, unknown.
        assert rects() == moved
        checks['a_reverted_arrangement_puts_every_display_back_and_overlaps_are_refused'] = True
        record['arrangement'] = {'start': start_rects, 'kept': moved}

        def outputs_now():
            return {o['name']: o for o in session.ipc(Path(state['backend_ipc']), 3) if o.get('active')}

        # A different shape for WL-1 (turned 90°), so mirroring needs bars, not just an overlay.
        assert control('display WL-1 rotate 90')['ok']
        wait(lambda: t if (t := topology()) and t.get('pending') and outputs_now()['WL-1']['transform'] == '90' else None)
        assert control('display keep')['ok']
        wait(lambda: t if (t := topology()) and not t.get('pending') else None)
        before_mirror = {n: o['scale'] for n, o in outputs_now().items()}

        def mirrored(pending):
            def ready():
                t = topology()
                if not t or t.get('pending') is not pending or [o['name'] for o in t['outputs']] != ['WL-2']:
                    return None
                return t if [m['name'] for m in t.get('mirrors', [])] == ['WL-1'] and 'canvas' in t else None
            return wait(ready)

        answer = control('display mirror on')
        assert answer['ok'], answer
        snapshot = mirrored(True)  # Applied: the desktop's window took the canvas size.
        record['snapshots'].append(snapshot)
        now = outputs_now()
        main, other = now['WL-2']['rect'], now['WL-1']['rect']
        assert (other['x'] <= main['x'] and other['y'] <= main['y'] and other['x'] + other['width'] >= main['x'] + main['width']
                and other['y'] + other['height'] >= main['y'] + main['height']), (main, other)
        assert abs((main['x'] - other['x']) - (other['width'] - main['width']) // 2) <= 1, (main, other)
        assert abs((main['y'] - other['y']) - (other['height'] - main['height']) // 2) <= 1, (main, other)
        assert snapshot['canvas'] == {'width': other['width'], 'height': other['height']}, snapshot['canvas']
        assert (main['width'], main['height']) != (other['width'], other['height'])  # Bars, not an overlay.
        assert control('display keep')['ok']
        mirrored(False)
        assert read(settings)['mode'] == 'mirror'
        checks['mirror_shows_the_main_display_centred_on_the_other_and_keep_saves_it'] = True

        assert control('display mirror off')['ok']
        wait(lambda: t if (t := topology()) and t.get('pending') and len(t['outputs']) == 2 and not t.get('mirrors') else None)
        assert control('display revert')['ok']
        mirrored(False)
        assert read(settings)['mode'] == 'mirror'
        assert control('display mirror off')['ok']
        snapshot = wait(lambda: t if (t := topology()) and t.get('pending') and len(t['outputs']) == 2 else None)
        assert control('display keep')['ok']
        wait(lambda: t if (t := topology()) and not t.get('pending') and len(t['outputs']) == 2 else None)
        session.describe_displays(list(outputs_now().values()), None)  # Side by side again: no overlap.
        assert {n: o['scale'] for n, o in outputs_now().items()} == before_mirror
        assert read(settings)['mode'] == 'extend'
        checks['mirror_off_reverts_then_keeps_each_display_its_own_size_side_by_side'] = True

        assert control('display WL-1 rotate normal')['ok']
        wait(lambda: t if (t := topology()) and t.get('pending') and outputs_now()['WL-1']['transform'] == 'normal' else None)
        assert control('display keep')['ok']
        wait(lambda: t if (t := topology()) and not t.get('pending') else None)

        answer = control('display WL-1 off')
        assert answer['ok'], answer
        settled(['WL-2'], True)
        started = time.monotonic()
        settled_back = wait(lambda: topology() if (t := topology()) and t.get('pending') is False
                            and active() == ['WL-1', 'WL-2'] else None, timeout=25)
        record['snapshots'].append(settled_back)
        assert 13 <= time.monotonic() - started <= 22, time.monotonic() - started
        assert read(state_file)['display_change'] == 'not kept; reverted'
        assert {k: v for k, v in read(settings).items() if k != 'displays'} == {'version': 2, 'mode': 'extend', 'off': []}
        checks['a_change_that_is_not_kept_reverts_by_itself_after_15_seconds'] = True

        assert control('display WL-2 off')['ok']
        snapshot = settled(['WL-1'], True)
        assert snapshot['primary'] == 'WL-1' and [d['name'] for d in snapshot['available']] == ['WL-2']
        assert control('display keep')['ok']
        settled(['WL-1'], False)
        saved = read(settings)
        assert saved['mode'] == 'extend' and [i['connector'] for i in saved['off']] == ['WL-2'], saved
        checks['the_main_display_can_be_turned_off_and_the_desktop_moves'] = True

        for refused in ('display WL-1 off', 'display WL-9 on', 'display WL-1; exit on', 'display WL-2 maybe',
                        'display keep', 'display revert'):
            answer = control(refused)
            assert not answer['ok'], refused
        assert active() == ['WL-1'] and read(settings) == saved
        checks['the_last_display_unknown_names_and_stray_requests_are_refused'] = True
        stop()

        start('second-login')
        snapshot = settled(['WL-1'], False)
        assert [d['name'] for d in snapshot['available']] == ['WL-2']
        checks['the_switched_off_monitor_stays_off_at_the_next_login'] = True
        assert control('display WL-2 on')['ok']
        settled(['WL-1', 'WL-2'], True)
        assert control('display revert')['ok']
        settled(['WL-1'], False)
        assert read(settings) == saved and read(state_file)['display_change'] == 'reverted'
        checks['revert_undoes_at_once_without_saving'] = True

        def output(name, snapshot):
            return next(o for o in snapshot['outputs'] if o['name'] == name)

        answer = control('display WL-1 scale 2')
        assert answer['ok'], answer
        wait(lambda: t if (t := topology()) and t.get('pending') and output('WL-1', t)['scale'] == 2 else None)
        assert control('display keep')['ok']
        snapshot = settled(['WL-1'], False)
        assert output('WL-1', snapshot)['scale'] == 2
        saved = read(settings)
        assert [d['scale'] for d in saved['displays'] if d['identity']['connector'] == 'WL-1'] == [2.0], saved
        checks['a_display_size_change_applies_at_once_and_keep_saves_it'] = True

        assert control('display WL-1 rotate 90')['ok']
        wait(lambda: t if (t := topology()) and t.get('pending') and output('WL-1', t)['transform'] == '90' else None)
        assert control('display revert')['ok']
        snapshot = wait(lambda: t if (t := topology()) and not t.get('pending')
                        and output('WL-1', t)['transform'] == 'normal' else None)
        assert output('WL-1', snapshot)['scale'] == 2 and read(settings) == saved
        checks['a_reverted_rotation_restores_the_previous_one_and_keeps_the_saved_size'] = True

        modes = snapshot['modes']['WL-1']
        record['nested_modes'] = modes
        for refused in ('display WL-1 mode 123x456@1000', 'display WL-1 mode 1920x1080', 'display WL-1 scale 9',
                        'display WL-1 scale 1e0', 'display WL-1 rotate flipped', 'display WL-2 scale 1.5'):
            assert not control(refused)['ok'], refused
        assert read(settings) == saved
        others = [m for m in modes['modes'] if m != modes.get('mode')]
        if others:
            choice = others[0]
            assert control(f"display WL-1 mode {choice['width']}x{choice['height']}@{choice['refresh']}")['ok']
            wait(lambda: t if (t := topology()) and t.get('pending') and t['modes']['WL-1'].get('mode') == choice else None)
            assert control('display revert')['ok']
            wait(lambda: t if (t := topology()) and not t.get('pending')
                 and t['modes']['WL-1'].get('mode') == modes.get('mode') else None)
            checks['a_resolution_change_applies_and_reverts'] = True
        checks['unknown_modes_bad_values_and_displays_that_are_off_are_refused'] = True
        stop()

        start('third-login')
        snapshot = settled(['WL-1'], False)
        assert output('WL-1', snapshot)['scale'] == 2
        checks['a_saved_size_is_used_at_the_next_login'] = True
        stop()
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            child.wait(timeout=20)
        (evidence / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
print('Display switching checks passed:', len(checks), 'Evidence:', evidence)
