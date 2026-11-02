#!/usr/bin/env python3
"""Read-only boot metrics in an explicitly named owned VM role, never the host.

Source candidate: no VM measurements have been run for this packet.
"""
import argparse
import json
from pathlib import Path
import platform
import socket
import subprocess
import time

ROLES = {'live': 'platinum-live', 'installed': 'pr-installed', 'builder': 'pr-iso-builder'}
ENV = {'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','LC_ALL':'C.UTF-8',
       'SYSTEMD_PAGER':'cat','SYSTEMD_COLORS':'0'}
LIMIT = 65536


def require_role(role):
    if role not in ROLES or socket.gethostname() != ROLES[role]:
        raise SystemExit('Boot metrics requires the explicitly named owned VM role')
    virtual = subprocess.run(['/usr/bin/systemd-detect-virt','--vm'], env=ENV,
                             capture_output=True,text=True,timeout=5)
    if virtual.returncode or virtual.stdout.strip() not in ('qemu','kvm'):
        raise SystemExit('Boot metrics refuses physical hosts and other hypervisors')
    container = subprocess.run(['/usr/bin/systemd-detect-virt','--container'], env=ENV,
                               capture_output=True,text=True,timeout=5)
    if container.returncode == 0:
        raise SystemExit('Boot metrics refuses containers')


def command(verb):
    try:
        result = subprocess.run(['/usr/bin/systemd-analyze','--no-pager',verb],env=ENV,
                                capture_output=True,text=True,timeout=10)
        return {'status':result.returncode,'stdout':result.stdout[:LIMIT],
                'stderr':result.stderr[:4096],
                'truncated':len(result.stdout)>LIMIT or len(result.stderr)>4096}
    except subprocess.TimeoutExpired:
        return {'status':124,'stdout':'','stderr':'VM metrics command timed out','truncated':False}
    except OSError as error:
        return {'status':127,'stdout':'','stderr':str(error)[:4096],'truncated':False}


def collect(run=command, now=time.monotonic, sleep=time.sleep):
    started = now(); attempts = 0
    # SSH/console collection is outside the system manager's startup dependency
    # graph. Wait boundedly for its real startup timestamp, never fake zero.
    while True:
        timing = run('time'); attempts += 1
        if timing['status'] == 0 or now()-started >= 30:
            break
        sleep(1)
    blame = run('blame'); chain = run('critical-chain'); version = run('--version')
    return {'version':1,'ok':all(item['status']==0 and not item['truncated']
                               for item in (timing,blame,chain,version)),
            'startup_complete':timing['status']==0,'timing_attempts':attempts,
            'collection_seconds':round(now()-started,3),
            'systemd_version':version['stdout'].splitlines()[:1],
            'version_command':version,
            'time':timing,'blame':blame,'critical_chain':chain,
            'top_10_blame':[line.strip() for line in blame['stdout'].splitlines() if line.strip()][:10],
            'scope':'Read-only VM observation; blame ordering is not a causal boot-time verdict'}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role',choices=tuple(ROLES),required=True)
    args=parser.parse_args(argv)
    require_role(args.role)  # Before /proc reads or system-manager queries.
    record=collect()
    record.update(role=args.role,hostname=socket.gethostname(),python=platform.python_version(),
                  uptime_seconds=float(Path('/proc/uptime').read_text().split()[0]))
    print(json.dumps(record))
    return 0 if record['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
