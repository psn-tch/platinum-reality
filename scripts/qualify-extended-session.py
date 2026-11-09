#!/usr/bin/env python3
"""Exercise opt-in live topology on an owned nested Sway, never a physical seat."""
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

# Never access the host HTTP daemon during installed-production-path checks.
os.environ["PLATINUM_DISABLE_HOST_HANDHELD"] = "1"

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--launcher',type=Path,required=True)
parser.add_argument('--evidence',type=Path,required=True)
options=parser.parse_args()
project=Path(__file__).resolve().parents[1]
owned=os.environ.get('FRANKENSTEIN_HEADLESS_IPC','')
if not owned.startswith('/tmp/platinum-headless.') or not Path(owned).is_socket():
    parser.error('Use with-headless-display.py; an owned headless venue is required')
launcher=options.launcher.resolve(strict=True)
if not launcher.is_relative_to(project/'build'):
    parser.error('Use a separate stage inside this project build directory')
evidence=options.evidence.absolute()
evidence.mkdir(parents=True,exist_ok=False,mode=0o700)
spec=importlib.util.spec_from_file_location('session',project/'scripts/standalone-session.py')
session=importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)
checks={}
record={'hardware_qualified':False,'checks':checks,'snapshots':[],
        'source_sha256':{str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (Path(__file__),project/'scripts/standalone-session.py',
                                   project/'src/desktop/main.cpp',project/'src/desktop/display_topology_feed.cpp')}}
library=launcher.parent.parent/'lib/platinum-reality'
record['payload_sha256']={str(p.relative_to(launcher.parent.parent)):hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in library.rglob('*') if p.is_file()}

def read(path):
    return json.loads(path.read_text()) if path.exists() else {}

def wait(predicate,timeout=20):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        value=predicate()
        if value:return value
        if child.poll() is not None:raise AssertionError('Owned session exited; inspect supervisor.txt and session/desktop.txt')
        time.sleep(0.05)
    raise AssertionError('Owned session condition timed out; inspect evidence')

def command(text):
    reply=session.ipc(Path(current['backend_ipc']),0,text)
    assert reply and all(r.get('success') for r in reply),text

def applied_after(generation):
    state=read(state_file)
    return state if (state.get('display_generation',0)>generation and
                     state.get('display_applied_generation')==state.get('display_generation')) else None

def save_snapshot(label,state):
    snapshot=read(Path(state['display_topology']))
    acknowledgement=read(Path(state['display_topology']+'.applied'))
    assert snapshot['generation']==acknowledgement['generation']==state['display_applied_generation']
    assert snapshot['session']==acknowledgement['session']
    record['snapshots'].append({'label':label,'snapshot':snapshot,'acknowledgement':acknowledgement})

def send_control(command):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(8);peer.connect(current['control']);peer.sendall(command.encode()+b'\n')
        return json.loads(peer.recv(8192))

with tempfile.TemporaryDirectory(prefix='platinum-extended-check-') as temporary:
    fixture=Path(temporary)
    environment=os.environ.copy()
    environment.update(WLR_WL_OUTPUTS='2',HOME=str(fixture/'home'),
                       XDG_CONFIG_HOME=str(fixture/'config'),XDG_DATA_HOME=str(fixture/'data'),
                       XDG_CACHE_HOME=str(fixture/'cache'),XDG_STATE_HOME=str(fixture/'state'))
    (fixture/'home').mkdir()
    state_file=evidence/'session/session.json'
    desktop_state=evidence/'desktop-state.json'
    arguments=['python3','-B',str(project/'scripts/standalone-session.py'),'--nested-check',
               '--experimental-extended','--primary-output','WL-2','--launcher',str(launcher),
               '--evidence',str(evidence/'session'),'--workspace',str(fixture/'Workspace'),
               '--profile',str(fixture/'FirstProfile'),'--shell-argument=--state-file',
               '--shell-argument='+str(desktop_state)]
    with (evidence/'supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            initial=session.ipc(Path(current['backend_ipc']),3)
            assert len([o for o in initial if o.get('active')])==2
            assert current['primary_output']=='WL-2'
            save_snapshot('initial-two-outputs',current)
            checks['live_snapshot_and_applied_ack']=True
            windows=wait(lambda:read(desktop_state).get('windows'))
            primary=next(o for o in record['snapshots'][-1]['snapshot']['outputs'] if o['name']=='WL-2')
            origin=min(o['x'] for o in record['snapshots'][-1]['snapshot']['outputs'])
            assert any(primary['x']-origin <= w['x'] < primary['x']-origin+primary['width'] for w in windows)
            checks['real_finder_starts_on_primary']=True
            previous=current['display_generation']
            assert not send_control('primary WL-999')['ok']
            assert not send_control('primary WL-2; exit')['ok']
            assert read(state_file)['display_generation']==previous
            assert not (fixture/'FirstProfile'/'main-display.json').exists()
            checks['invalid_primary_request_does_not_change_layout']=True
            assert send_control('primary WL-1')['ok']
            current=wait(lambda:applied_after(previous))
            assert current['primary_output']=='WL-1'
            save_snapshot('chosen-primary',current)
            previous=current['display_generation']
            assert send_control('primary WL-2')['ok']
            current=wait(lambda:applied_after(previous))
            checks['runtime_primary_selection_acknowledged']=True
            saved_primary=read(fixture/'FirstProfile'/'main-display.json')
            assert saved_primary['connector']=='WL-2'
            assert (fixture/'FirstProfile'/'main-display.json').stat().st_mode & 0o777 == 0o600
            checks['chosen_primary_is_saved_privately']=True
            previous=current['display_generation']
            command('output WL-2 scale 2')
            current=wait(lambda:applied_after(previous))
            save_snapshot('changed-scale',current)
            checks['live_scale_change_acknowledged']=True
            previous=current['display_generation']
            command('output WL-2 disable')
            current=wait(lambda:applied_after(previous))
            assert current['primary_output']=='WL-1'
            save_snapshot('primary-removed',current)
            remaining=record['snapshots'][-1]['snapshot']['outputs'][0]
            wait(lambda:any(0<=w['x']<=remaining['width']-80 for w in read(desktop_state).get('windows',[])))
            checks['primary_removal_and_window_recovery']=True
            command('output WL-1 disable')
            wait(lambda:read(state_file).get('display_error'))
            assert child.poll() is None
            checks['no_outputs_does_not_end_session']=True
            command('output WL-1 enable')
            current=wait(lambda:s if (s:=read(state_file)).get('ready') and not s.get('display_error') else None)
            command('output WL-2 enable')
            previous=current['display_generation']
            current=wait(lambda:applied_after(previous))
            assert current['primary_output']=='WL-2'
            save_snapshot('primary-reconnected',current)
            checks['reconnect_restores_preferred_primary']=True
            assert send_control('logout')['ok']
            assert child.wait(timeout=20)==0
            finished=read(state_file)
            assert finished['finished'] and finished['runtime_removed']
            assert not Path(finished['runtime']).exists()
            checks['normal_shutdown_and_private_runtime_cleanup']=True
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    # A second session: the monitors KDE saved for this combination are arranged as KDE had them
    # (positions, and its main display over the --primary-output hint). The nested outputs have no
    # EDID, so their KDE entries are matched by connector name.
    (fixture/'config').mkdir(exist_ok=True)
    (fixture/'config'/'kwinoutputconfig.json').write_text(json.dumps([
        {'name':'outputs','data':[{'connectorName':'WL-1','transform':'Normal','scale':1},
                                  {'connectorName':'WL-2','transform':'Normal','scale':1}]},
        {'name':'setups','data':[{'lidClosed':False,'outputs':[
            {'outputIndex':0,'enabled':True,'position':{'x':2000,'y':100},'priority':1},
            {'outputIndex':1,'enabled':True,'position':{'x':0,'y':0},'priority':2}]}]}]))
    state_file=evidence/'kde-session/session.json'
    arguments=[str(evidence/'kde-session') if a==str(evidence/'session') else
               str(fixture/'Profile') if a==str(fixture/'FirstProfile') else a for a in arguments]
    with (evidence/'kde-supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            placed={o['name']:o for o in session.ipc(Path(current['backend_ipc']),3)}
            assert (placed['WL-1']['rect']['x'],placed['WL-1']['rect']['y'])==(2000,100), placed['WL-1']['rect']
            assert (placed['WL-2']['rect']['x'],placed['WL-2']['rect']['y'])==(0,0), placed['WL-2']['rect']
            assert current['primary_output']=='WL-1' and current['kde_arrangement']['WL-1']['primary']
            save_snapshot('kde-arrangement',current)
            checks['kde_arrangement_and_main_display_applied']=True
            assert send_control('logout')['ok']
            assert child.wait(timeout=20)==0
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    # A third session: Lily Hewitt's 2026-10-22 login. KDE kept the only enabled monitor at x=696 because
    # the disabled laptop panel sat at the origin. Sway draws the global-fullscreen desktop from
    # the layout origin, so before the fix the right 696 columns were black.
    (fixture/'config'/'kwinoutputconfig.json').write_text(json.dumps([
        {'name':'outputs','data':[{'connectorName':'WL-1','transform':'Normal','scale':1},
                                  {'connectorName':'WL-2','transform':'Normal','scale':1}]},
        {'name':'setups','data':[{'lidClosed':False,'outputs':[
            {'outputIndex':0,'enabled':True,'position':{'x':696,'y':0},'priority':1},
            {'outputIndex':1,'enabled':False,'position':{'x':0,'y':0},'priority':2}]}]}]))
    state_file=evidence/'origin-session/session.json'
    desktop_state=evidence/'origin-desktop-state.json'
    arguments=[str(evidence/'origin-session') if a==str(evidence/'kde-session') else
               '--shell-argument='+str(desktop_state) if a.startswith('--shell-argument='+str(evidence)) else a
               for a in arguments]
    with (evidence/'origin-supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            placed={o['name']:o for o in session.ipc(Path(current['backend_ipc']),3)}
            assert placed['WL-1']['active'] and not placed['WL-2']['active']
            assert (placed['WL-1']['rect']['x'],placed['WL-1']['rect']['y'])==(0,0), placed['WL-1']['rect']
            assert current['kde_arrangement']['WL-1']['x']==696
            assert current['display_origin_normalized']=={'x':696,'y':0}
            save_snapshot('kde-offset-origin',current)
            assert [(o['x'],o['y']) for o in record['snapshots'][-1]['snapshot']['outputs']]==[(0,0)]
            checks['offset_kde_arrangement_moved_to_origin']=True
            wait(lambda:read(desktop_state).get('windows'))
            time.sleep(1)
            capture=evidence/'origin-wl1.ppm'
            subprocess.run(['grim','-t','ppm','-o','WL-1',str(capture)],check=True,
                           env={**environment,'WAYLAND_DISPLAY':current['backend_display']})
            fields=capture.read_bytes().split(maxsplit=4)
            assert fields[0]==b'P6' and fields[3]==b'255'
            width,height,pixels=int(fields[1]),int(fields[2]),fields[4]
            assert len(pixels)==width*height*3 and width>697
            def drawn(x):
                return any(sum(pixels[(y*width+x)*3:(y*width+x)*3+3])>=24 for y in range(0,height,4))
            assert drawn(width-1) and drawn(width-697),'The right edge of the desktop is black'
            capture.unlink()
            checks['offset_origin_desktop_fills_the_screen']=True
            assert send_control('logout')['ok']
            assert child.wait(timeout=20)==0
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    # Explicit Platinum Use-all overrides KDE's disabled-panel choice. The first
    # three sessions above retain the experimental/KDE-compatible default.
    (fixture/'Profile'/'displays.json').write_text(json.dumps({'version':1,'mode':'extend'}))
    (fixture/'config'/'kwinoutputconfig.json').write_text(json.dumps([
        {'name':'outputs','data':[{'connectorName':'WL-1','transform':'Normal','scale':1},
                                  {'connectorName':'WL-2','transform':'Rotated90','scale':2.3}]},
        {'name':'setups','data':[{'lidClosed':False,'outputs':[
            {'outputIndex':0,'enabled':True,'position':{'x':696,'y':0},'priority':1},
            {'outputIndex':1,'enabled':False,'position':{'x':0,'y':0},'priority':2}]}]}]))
    state_file=evidence/'all-connected-session/session.json'
    desktop_state=evidence/'all-connected-desktop-state.json'
    arguments=[str(evidence/'all-connected-session') if a==str(evidence/'origin-session') else
               '--shell-argument='+str(desktop_state) if a.startswith('--shell-argument='+str(evidence)) else a
               for a in arguments]
    with (evidence/'all-connected-supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            placed={o['name']:o for o in session.ipc(Path(current['backend_ipc']),3)}
            assert placed['WL-1']['active'] and placed['WL-2']['active'], 'Use-all left KDE-disabled screen off'
            assert current['display_arrangement_source']=='platinum-all-connected'
            assert current['kde_arrangement']['WL-2']['enabled'] is False
            checks['explicit_use_all_overrides_kde_disabled_panel']=True
            assert placed['WL-2']['transform']=='270' and abs(placed['WL-2']['scale']-2.3)<0.001
            save_snapshot('all-connected-rotated-panel',current)
            description=record['snapshots'][-1]['snapshot']
            session.describe_displays(list(placed.values()),current['primary_output'])
            assert len(description['outputs'])==2
            assert min(o['x'] for o in description['outputs'])==0
            assert min(o['y'] for o in description['outputs'])==0
            assert placed['WL-1']['rect']['x']==0
            assert placed['WL-2']['rect']['x']==placed['WL-1']['rect']['width']
            checks['newly_enabled_panel_keeps_rotation_scale_and_safe_layout']=True
            # The connector can stay listed while its active state drops. The
            # explicit all-displays policy must notice that, not cache names only.
            command('output WL-2 disable')
            wait(lambda:any(o.get('name')=='WL-2' and o.get('active') for o in
                            session.ipc(Path(current['backend_ipc']),3)))
            current=wait(lambda:s if (s:=read(state_file)).get('display_applied_generation')==s.get('display_generation')
                         and not s.get('display_error') else None)
            assert len(read(Path(current['display_topology']))['outputs'])==2
            checks['use_all_recovers_a_connected_but_inactive_display']=True
            # A saved per-output scale changes logical sizes without changing
            # connector names. Repack the newly enabled panel beside the resized screen.
            preferences=json.loads((fixture/'config'/'kwinoutputconfig.json').read_text())
            preferences[0]['data'][0]['scale']=0.8
            (fixture/'config'/'kwinoutputconfig.json').write_text(json.dumps(preferences))
            previous=current['display_generation']
            current=wait(lambda:applied_after(previous))
            placed={o['name']:o for o in session.ipc(Path(current['backend_ipc']),3)}
            assert abs(placed['WL-1']['scale']-0.8)<0.001
            assert placed['WL-2']['rect']['x']==placed['WL-1']['rect']['width']
            save_snapshot('connected-output-resized',current)
            checks['logical_size_changes_rearrange_without_overlap']=True
            wait(lambda:read(desktop_state).get('windows'))
            assert send_control('logout')['ok']
            assert child.wait(timeout=20)==0
            assert read(state_file)['runtime_removed']
            checks['all_connected_session_runs_finder_and_closes_cleanly']=True
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    # An unusable KDE seed must not strand the user on a giant canvas or repeatedly
    # reapply it. This requests no giant buffers: validation rejects positions first.
    (fixture/'config'/'kwinoutputconfig.json').write_text(json.dumps([
        {'name':'outputs','data':[{'connectorName':'WL-1','transform':'Normal','scale':1},
                                  {'connectorName':'WL-2','transform':'Normal','scale':1}]},
        {'name':'setups','data':[{'lidClosed':False,'outputs':[
            {'outputIndex':0,'enabled':True,'position':{'x':0,'y':0},'priority':1},
            {'outputIndex':1,'enabled':True,'position':{'x':30000,'y':0},'priority':2}]}]}]))
    state_file=evidence/'bad-layout-session/session.json'
    desktop_state=evidence/'bad-layout-desktop-state.json'
    arguments=[str(evidence/'bad-layout-session') if a==str(evidence/'all-connected-session') else
               '--shell-argument='+str(desktop_state) if a.startswith('--shell-argument='+str(evidence)) else a
               for a in arguments]
    with (evidence/'bad-layout-supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            assert current.get('display_configuration_error')
            placed=session.ipc(Path(current['backend_ipc']),3)
            assert len([o for o in placed if o.get('active')])==2
            assert max(o['rect']['x']+o['rect']['width'] for o in placed)<16384
            save_snapshot('unusable-seed-restored',current)
            checks['unusable_saved_layout_restores_previous_outputs']=True
            wait(lambda:read(desktop_state).get('windows'))
            assert send_control('logout')['ok']
            assert child.wait(timeout=20)==0 and read(state_file)['runtime_removed']
            checks['rejected_layout_still_runs_finder_and_logs_out']=True
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    # A new login restores the first session's choice even when both the launcher
    # hint and KDE's main display disagree. Only this fixture's private profile
    # and nested backend are used, never a physical output or user's settings.
    (fixture/'config'/'kwinoutputconfig.json').write_text(json.dumps([
        {'name':'outputs','data':[{'connectorName':'WL-1','transform':'Normal','scale':1},
                                  {'connectorName':'WL-2','transform':'Normal','scale':1}]},
        {'name':'setups','data':[{'lidClosed':False,'outputs':[
            {'outputIndex':0,'enabled':True,'position':{'x':0,'y':0},'priority':1},
            {'outputIndex':1,'enabled':True,'position':{'x':1120,'y':0},'priority':2}]}]}]))
    state_file=evidence/'restored-primary-session/session.json'
    desktop_state=evidence/'restored-primary-desktop-state.json'
    arguments=[str(evidence/'restored-primary-session') if a==str(evidence/'bad-layout-session') else
               str(fixture/'FirstProfile') if a==str(fixture/'Profile') else
               'WL-1' if a=='WL-2' else
               '--shell-argument='+str(desktop_state) if a.startswith('--shell-argument='+str(evidence)) else a
               for a in arguments]
    primary_path=fixture/'FirstProfile'/'main-display.json'
    with (evidence/'restored-primary-supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            assert current['primary_output']=='WL-2' and current['main_display_preference']=='matched'
            assert current['kde_arrangement']['WL-1']['primary']
            save_snapshot('primary-restored-after-login',current)
            checks['saved_primary_wins_over_kde_and_launcher_after_login']=True
            saved_bytes=primary_path.read_bytes()
            previous=current['display_generation']
            command('output WL-2 disable')
            current=wait(lambda:applied_after(previous))
            assert current['primary_output']=='WL-1' and current['main_display_preference']=='unavailable'
            assert primary_path.read_bytes()==saved_bytes
            previous=current['display_generation']
            command('output WL-2 enable')
            current=wait(lambda:applied_after(previous))
            assert current['primary_output']=='WL-2' and primary_path.read_bytes()==saved_bytes
            checks['unavailable_saved_primary_survives_and_returns']=True
            previous=current['display_generation']
            assert send_control('primary WL-1')['ok']
            current=wait(lambda:applied_after(previous))
            assert current['primary_output']=='WL-1' and read(primary_path)['connector']=='WL-1'
            checks['later_primary_choice_replaces_saved_preference']=True
            wait(lambda:read(desktop_state).get('windows'))
            assert send_control('logout')['ok'] and child.wait(timeout=20)==0
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    # A rejected settings write must not silently change the live main display.
    # A directory occupying only the fixture file makes failure deterministic,
    # including on privileged test accounts. Later successful requests recover.
    state_file=evidence/'primary-save-error-session/session.json'
    desktop_state=evidence/'primary-save-error-desktop-state.json'
    arguments=[str(evidence/'primary-save-error-session') if a==str(evidence/'restored-primary-session') else
               'WL-2' if a=='WL-1' else
               '--shell-argument='+str(desktop_state) if a.startswith('--shell-argument='+str(evidence)) else a
               for a in arguments]
    with (evidence/'primary-save-error-supervisor.txt').open('wb') as log:
        child=subprocess.Popen(arguments,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            current=wait(lambda:s if (s:=read(state_file)).get('ready') else None)
            assert current['primary_output']=='WL-1'
            saved_bytes=primary_path.read_bytes()
            primary_path.unlink()
            primary_path.mkdir()
            answer=send_control('primary WL-2')
            assert not answer['ok'] and answer['primary_output']=='WL-1' and answer['main_display_save_error']
            current=wait(lambda:s if (s:=read(state_file)).get('display_applied_generation')==s.get('display_generation') else None)
            save_snapshot('failed-primary-save-rolled-back',current)
            assert primary_path.is_dir()
            checks['failed_primary_save_reports_failure_and_restores_live_choice']=True
            primary_path.rmdir()
            primary_path.write_bytes(saved_bytes)
            (fixture/'FirstProfile'/'displays.json').write_text('{"version":1,"mode":"extend"}\n')
            previous=current['display_generation']
            answer=send_control('primary WL-2')
            assert answer['ok'] and 'main_display_save_error' not in answer
            current=wait(lambda:applied_after(previous))
            assert read(primary_path)['connector']=='WL-2'
            assert (fixture/'FirstProfile'/'displays.json').read_text()=='{"version":1,"mode":"extend"}\n'
            wait(lambda:read(desktop_state).get('windows'))
            assert send_control('logout')['ok'] and child.wait(timeout=20)==0
            assert read(state_file)['runtime_removed']
            checks['primary_save_recovers_without_overwriting_use_all_and_logs_out']=True
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=20)
            (evidence/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
print('Extended session checks passed:',len(checks),'Evidence:',evidence)
