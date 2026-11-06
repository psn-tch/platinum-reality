#!/usr/bin/env python3
"""Measure the context-menu test alone, with fresh Firefox per iteration.

Run in a socket/DRM-capable venue. Holds the explicitly named shared lease only
while testing. Every failure/skip is retained and prevents a successful result.
No host browser profile, session, packages, or selected release is changed.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parent.parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--lease',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--runs',type=int,default=200)
    args=parser.parse_args()
    if args.runs<200:parser.error('At least 200 measured iterations are required')
    build=args.build.resolve(strict=True)
    args.evidence=args.evidence.resolve()
    args.evidence.mkdir(mode=0o700,parents=True,exist_ok=False)
    report={'ok':False,'requested':args.runs,'runs':[],
            'native_source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'source_dirty':bool(subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain','--untracked-files=no'])),
            'test_source_sha256':hashlib.sha256((ROOT/'tests/native_tests.cpp').read_bytes()).hexdigest(),
            'test_binary_sha256':hashlib.sha256((build/'native-tests').read_bytes()).hexdigest()}
    home=args.evidence/'home';home.mkdir()
    for name in ('config','data','cache','state','runtime'):(home/name).mkdir(mode=0o700)
    env={'HOME':str(home),'USER':os.environ.get('USER','tester'),'PATH':'/usr/local/bin:/usr/bin:/bin',
         'LANG':'en_US.UTF-8','SHELL':'/bin/bash','XDG_RUNTIME_DIR':str(home/'runtime'),
         'XDG_CONFIG_HOME':str(home/'config'),'XDG_DATA_HOME':str(home/'data'),
         'XDG_CACHE_HOME':str(home/'cache'),'XDG_STATE_HOME':str(home/'state'),
         'PLATINUM_DISABLE_HOST_HANDHELD':'1',
         'DBUS_SYSTEM_BUS_ADDRESS':'unix:path='+str(args.evidence/'no-system-bus'),
         'FRANKENSTEIN_NATIVE_BUILD_DIR':str(build)}
    try:
        with args.lease.resolve(strict=True).open('a') as lease:
            # Never enqueue an unattended long run over another gate.
            fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
            for number in range(1,args.runs+1):
                run=args.evidence/f'{number:04d}'
                command=['python3','-B',str(ROOT/'scripts/with-headless-display.py'),
                    '--timeout','120','--evidence',str(run),'--','bash',
                    str(ROOT/'scripts/qualify-native.sh'),'firefoxContextMenuSavesImage','-maxwarnings','0']
                started=time.monotonic()
                with (args.evidence/f'{number:04d}.log').open('x') as log:
                    result=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=140)
                output=(run/'command.txt').read_text() if (run/'command.txt').exists() else ''
                # Init, the single measured test, cleanup; skips cannot count as passes.
                ok=result.returncode==0 and bool(re.search(r'Totals: 3 passed, 0 failed, 0 skipped,',output))
                row={'iteration':number,'ok':ok,'exit':result.returncode,'seconds':round(time.monotonic()-started,3)}
                report['runs'].append(row)
                (args.evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
                print(json.dumps(row),flush=True)
                # A failed venue cannot measure a browser flake. Preserve it and stop.
                if not (run/'command.txt').is_file():break
        report['ok']=len(report['runs'])==args.runs and all(row['ok'] for row in report['runs'])
        return 0 if report['ok'] else 1
    finally:
        (args.evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    raise SystemExit(main())
