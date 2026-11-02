#!/usr/bin/env python3
"""Read live test-VM topology and authenticated backend output IPC; no writes."""
import json,math,os
from pathlib import Path
import socket,stat,struct,subprocess


def require_live():
    if os.geteuid()==0 or socket.gethostname()!='platinum-live':
        raise SystemExit('Unprivileged owned live VM only')
    vm=subprocess.run(['systemd-detect-virt','--vm'],capture_output=True,text=True,timeout=5)
    container=subprocess.run(['systemd-detect-virt','--container'],capture_output=True,text=True,timeout=5)
    if vm.returncode or vm.stdout.strip() not in ('qemu','kvm') or container.returncode!=1:
        raise SystemExit('Non-container QEMU/KVM live VM required')
    if not Path('/run/archiso/bootmnt').is_mount():raise SystemExit('Actual live media required')


def read_json(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size>262144:
        raise ValueError('Expected bounded regular VM JSON')
    return json.loads(path.read_text())


def read_exact(peer,count):
    data=bytearray()
    while len(data)<count:
        part=peer.recv(count-len(data))
        if not part:raise ValueError('Backend IPC closed before its full response')
        data.extend(part)
    return bytes(data)


def backend_outputs(path,pid):
    info=path.lstat()
    if not stat.S_ISSOCK(info.st_mode) or info.st_uid!=os.getuid():raise ValueError('Unexpected backend endpoint')
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(5);peer.connect(str(path))
        owner,uid,_=struct.unpack('3i',peer.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
        if owner!=pid or uid!=os.getuid():raise ValueError('Backend peer does not match the live session')
        peer.sendall(b'i3-ipc'+struct.pack('<II',0,3)) # Read-only GET_OUTPUTS.
        header=read_exact(peer,14)
        if header[:6]!=b'i3-ipc':raise ValueError('Wrong backend protocol')
        size,kind=struct.unpack('<II',header[6:])
        if kind!=3 or size>1048576:raise ValueError('Wrong or oversized backend output response')
        result=json.loads(read_exact(peer,size))
    if not isinstance(result,list) or len(result)>32 or any(not isinstance(o,dict) for o in result):
        raise ValueError('Unexpected backend output list')
    return result


def collect():
    uid=os.getuid();root=Path('/home/liveuser/.local/state/platinum-reality/session-runs')
    states=[]
    for folder in sorted(root.iterdir())[-32:]:
        if folder.is_symlink() or not folder.is_dir():continue
        path=folder/'session.json'
        if not path.exists():continue
        state=read_json(path)
        if state.get('ready') is not True:continue
        pid=state.get('supervisor_pid')
        if type(pid) is not int or pid<=1:continue
        try:
            if Path('/proc/'+str(pid)).stat().st_uid!=uid:continue
        except FileNotFoundError:continue
        states.append(state)
    if len(states)!=1:raise ValueError('Need exactly one active ready VM session')
    state=states[0];runtime=Path(state['runtime'])
    if not runtime.is_relative_to('/run/user/'+str(uid)) or runtime.resolve(strict=True)!=runtime:
        raise ValueError('Unexpected session runtime')
    paths=[Path(state[k]) for k in ('display_topology','backend_ipc')]
    if any(not p.is_relative_to(runtime) or p.resolve(strict=True)!=p for p in paths):
        raise ValueError('Session data escaped its runtime')
    topology=read_json(paths[0]);outputs=backend_outputs(paths[1],state['backend_pid'])
    return {'version':1,'session_ready':True,'primary':state.get('primary_output'),
            'backend_outputs':outputs,'topology':topology,
            'display_generation':state.get('display_generation'),
            'display_applied_generation':state.get('display_applied_generation'),
            'scope':'Read-only live-VM enumeration; no installed login, Monitors UI interaction, mirror or persistence claim'}


def matching_geometry(active,native):
    """Compare actual logical rectangles, scale and transform, not just names.

    These are read-only observations. A mismatching sample fails rather than
    configuring a display, repairing the topology or trusting capacity.
    """
    transforms={'normal','90','180','270','flipped','flipped-90','flipped-180','flipped-270'}
    prior=[]
    for backend,desktop in zip(sorted(active,key=lambda o:o['name']),
                               sorted(native,key=lambda o:o['name'])):
        rect=backend.get('rect')
        if not isinstance(rect,dict):return False
        for key in ('x','y','width','height'):
            if (type(rect.get(key)) is not int or type(desktop.get(key)) is not int or
                    rect[key]!=desktop[key]):return False
        if (not -16384<=rect['x']<=16384 or not -16384<=rect['y']<=16384 or
                not 80<=rect['width']<=16384 or not 80<=rect['height']<=16384):return False
        for output in (backend,desktop):
            scale=output.get('scale')
            if (type(scale) not in (int,float) or not math.isfinite(scale) or not .5<=scale<=4 or
                    output.get('transform') not in transforms):return False
        if backend['scale']!=desktop['scale'] or backend['transform']!=desktop['transform']:return False
        if any(rect['x']<p['x']+p['width'] and p['x']<rect['x']+rect['width'] and
               rect['y']<p['y']+p['height'] and p['y']<rect['y']+rect['height'] for p in prior):return False
        prior.append(rect)
    if not prior:return False
    width=max(o['x']+o['width'] for o in prior)-min(o['x'] for o in prior)
    height=max(o['y']+o['height'] for o in prior)-min(o['y'] for o in prior)
    return width<=16384 and height<=16384 and width*height<=64*1024*1024


def assess(record,expected):
    if type(expected) is not int or expected not in (1,2):raise ValueError('One or two requested outputs required')
    outputs=record.get('backend_outputs',[]);topology=record.get('topology',{})
    active=[o for o in outputs if isinstance(o,dict) and o.get('active') is True and not o.get('non_desktop',False)]
    backend_names=[o.get('name') for o in active]
    native=[o for o in topology.get('outputs',[]) if isinstance(o,dict)]
    native_names=[o.get('name') for o in native]
    generation=record.get('display_generation')
    generations=(generation,record.get('display_applied_generation'),topology.get('generation'))
    applied=(all(type(value) is int and value>0 for value in generations) and len(set(generations))==1)
    names_ok=(0<len(backend_names)==len(native_names) and
              all(isinstance(n,str) and n for n in backend_names+native_names) and
              len(set(backend_names))==len(backend_names) and len(set(native_names))==len(native_names) and
              set(backend_names)==set(native_names))
    count_ok=len(backend_names)==expected
    geometry_ok=bool(names_ok and matching_geometry(active,native))
    primary=topology.get('primary')
    primary_ok=bool(names_ok and isinstance(primary,str) and primary in backend_names and record.get('primary')==primary)
    return {'backend_active_count':len(active),'backend_names':backend_names,'desktop_topology_names':native_names,
            'desktop_generation_acknowledged':applied,
            'backend_and_desktop_geometry_match':geometry_ok,'primary_output_matches':primary_ok,
            'all_requested_outputs_active_and_applied':bool(names_ok and count_ok and applied and geometry_ok and primary_ok and topology.get('mode')=='extend'),
            'scope':'Live backend and acknowledged native topology only; independent screen/UI/interaction review remains required'}


def main():
    require_live();print(json.dumps(collect()))


if __name__=='__main__':main()
