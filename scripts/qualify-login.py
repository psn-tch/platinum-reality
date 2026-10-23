#!/usr/bin/env python3
"""--start-locked (Log In replacing the tty1 text login) on an owned nested Sway, never a seat.

Nothing here authenticates: the real Platinum Log In is only observed and ended, swaylock is
released with its own SIGUSR1, and a stand-in Log In reports a session choice. Checks that the
desktop starts only after Log In, that KDE ends the supervisor with its exit code before any
desktop, and that failed Log Ins end the session without a desktop.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--launcher', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
options = parser.parse_args()
project = Path(__file__).resolve().parents[1]
owned = os.environ.get('FRANKENSTEIN_HEADLESS_IPC', '')
if not owned.startswith('/tmp/platinum-headless.') or not Path(owned).is_socket():
    parser.error('Use with-headless-display.py; an owned headless venue is required')
launcher = options.launcher.resolve(strict=True)
if not launcher.is_relative_to(project / 'build'):
    parser.error('Use a separate stage inside this project build directory')
evidence = options.evidence.absolute()
evidence.mkdir(parents=True, exist_ok=False, mode=0o700)
spec = importlib.util.spec_from_file_location('session', project / 'scripts/standalone-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)
checks = {}


def read(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def wait(predicate, child, timeout=20):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        value = predicate()
        if value:
            return value
        if child.poll() is not None:
            raise AssertionError('Supervisor exited early; inspect evidence')
        time.sleep(0.05)
    raise AssertionError('Timed out; inspect evidence')


def supervise(name, stage_launcher, fixture, environment):
    arguments = ['python3', '-B', str(project / 'scripts/standalone-session.py'), '--nested-check',
                 '--start-locked', '--launcher', str(stage_launcher), '--evidence', str(evidence / name),
                 '--workspace', str(fixture / 'Workspace'), '--profile', str(fixture / 'Profile')]
    log = (evidence / (name + '-supervisor.txt')).open('wb')
    return subprocess.Popen(arguments, env=environment, stdout=log, stderr=subprocess.STDOUT), log


with tempfile.TemporaryDirectory(prefix='platinum-login-check-') as temporary:
    fixture = Path(temporary)
    environment = os.environ.copy()
    for part in ('home', 'config', 'data', 'cache', 'state'):
        (fixture / part).mkdir()
    # Never the user's real PAM conversation, fingerprint reader or system bus.
    environment.update(HOME=str(fixture / 'home'), XDG_CONFIG_HOME=str(fixture / 'config'),
                       XDG_DATA_HOME=str(fixture / 'data'), XDG_CACHE_HOME=str(fixture / 'cache'),
                       XDG_STATE_HOME=str(fixture / 'state'),
                       DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(fixture / 'no-host-system-bus'))

    # 1. The real Platinum Log In holds the screen; no desktop exists until Log In succeeds.
    state_file = evidence / 'real/session.json'
    child, log = supervise('real', launcher, fixture, environment)
    try:
        state = wait(lambda: s if (s := read(state_file)).get('locker') == 'platinum-login' else None, child)
        assert (Path(f"/proc/{state['lock_pid']}/exe").resolve()
                == (launcher.parent.parent / 'lib/platinum-reality/platinum-lock').resolve())
        arguments = Path(f"/proc/{state['lock_pid']}/cmdline").read_bytes().split(b'\0')
        assert b'--login' in arguments and b'--session-fd' in arguments
        time.sleep(1)
        state = read(state_file)
        assert state['locked'] and 'launcher_pid' not in state and not state['ready'], state
        checks['desktop_absent_while_platinum_log_in_runs'] = True
        # A crashed Log In is not a Log In: swaylock authenticates instead, still before any desktop.
        os.kill(state['lock_pid'], signal.SIGKILL)
        state = wait(lambda: s if (s := read(state_file)).get('locker') == 'swaylock-login' else None, child)
        time.sleep(0.5)
        assert 'launcher_pid' not in read(state_file)
        checks['crashed_log_in_falls_back_to_swaylock_without_desktop'] = True
        os.kill(state['lock_pid'], signal.SIGUSR1)  # swaylock's own unlock, standing in for a password
        state = wait(lambda: s if (s := read(state_file)).get('ready') else None, child, timeout=30)
        assert state['login_session'] == 'platinum' and not state['locked'] and state.get('launcher_pid')
        checks['desktop_starts_only_after_log_in'] = True
        log_text = (evidence / 'real/supervisor.txt').read_text()
        assert 'login: started Platinum Log In' in log_text and 'authenticated by swaylock' in log_text
        with __import__('socket').socket(__import__('socket').AF_UNIX) as peer:
            peer.settimeout(8)
            peer.connect(state['control'])
            peer.sendall(b'logout\n')
            assert json.loads(peer.recv(8192))['ok']
        assert child.wait(timeout=20) == 0
        checks['normal_logout_after_log_in'] = True
    finally:
        if child.poll() is None:
            child.terminate()
            child.wait(timeout=20)
        log.close()

    # Stand-in Log Ins in a copy of the stage: one chooses KDE, one fails.
    stage = fixture / 'stage'
    shutil.copytree(launcher.parent.parent, stage, symlinks=True)
    fake = stage / 'lib/platinum-reality/platinum-lock'

    def stand_in(body):
        fake.unlink()
        fake.write_text('#!/usr/bin/env python3\nimport os, sys\n'
                        'arguments = sys.argv[1:]\n'
                        'session = int(arguments[arguments.index("--session-fd") + 1])\n' + body)
        fake.chmod(0o755)

    # 2. KDE chosen at Log In: exit KDE_SESSION_EXIT, no desktop, private runtime cleaned up.
    stand_in('os.write(session, b"kde\\n")\nsys.exit(0)\n')
    child, log = supervise('kde', stage / 'bin/platinum-reality', fixture, environment)
    try:
        assert child.wait(timeout=30) == session.KDE_SESSION_EXIT
    finally:
        if child.poll() is None:
            child.kill()
        log.close()
    state = read(evidence / 'kde/session.json')
    assert state['login_session'] == 'kde' and 'launcher_pid' not in state and state['runtime_removed'], state
    checks['kde_choice_ends_supervisor_before_any_desktop'] = True

    # 3. Both Log Ins fail: the session ends, never a desktop (and never a shell: the caller exits).
    stand_in('sys.exit(1)\n')
    state_file = evidence / 'failed/session.json'
    child, log = supervise('failed', stage / 'bin/platinum-reality', fixture, environment)
    try:
        state = wait(lambda: s if (s := read(state_file)).get('locker') == 'swaylock-login' else None, child)
        os.kill(state['lock_pid'], signal.SIGKILL)
        assert child.wait(timeout=30) not in (0, session.KDE_SESSION_EXIT)
    finally:
        if child.poll() is None:
            child.kill()
        log.close()
    state = read(state_file)
    assert 'launcher_pid' not in state and state['runtime_removed'], state
    checks['failed_log_ins_end_the_session_without_desktop'] = True

(evidence / 'manifest.json').write_text(json.dumps({'checks': checks, 'hardware_qualified': False}, indent=2) + '\n')
print('Log In checks passed:', len(checks), 'Evidence:', evidence)
