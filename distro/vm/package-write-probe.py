#!/usr/bin/env python3
"""Read-only evidence of package writes in the owned installer live VM.

Never run this on a host. Not an installer or a process-signalling tool.
"""
import json
import os
from pathlib import Path
import re
import socket
import subprocess


def require_live():
    if os.geteuid() != 0 or socket.gethostname() != 'platinum-live':
        raise SystemExit('Package-write probe requires the owned root live fixture')
    virtual=subprocess.run(['/usr/bin/systemd-detect-virt','--vm'],capture_output=True,text=True,timeout=5)
    if virtual.returncode or virtual.stdout.strip() not in ('qemu','kvm'):
        raise SystemExit('Package-write probe requires QEMU/KVM')
    container=subprocess.run(['/usr/bin/systemd-detect-virt','--container'],capture_output=True,text=True,timeout=5)
    if container.returncode != 1:
        raise SystemExit('Package-write probe refuses containers or an indeterminate environment')
    if not Path('/run/archiso/bootmnt').is_mount():
        raise SystemExit('Package-write probe requires actual Arch live media')


def observe(runtime=Path('/run'), proc=Path('/proc')):
    # Read only the installer's fixed target path. Missing/ambiguous state is
    # not a pass; a progress event alone predates pacstrap's actual writes.
    roots=[p/'target' for p in runtime.glob('platinum-install-*')
           if not p.is_symlink() and p.is_dir() and (p/'target').is_mount()]
    active=[]
    for path in proc.iterdir():
        if not path.name.isdigit():continue
        try:
            if (path/'comm').read_text().strip() == 'pacman':active.append(int(path.name))
        except (FileNotFoundError,ProcessLookupError,PermissionError):
            continue
    record={'version':1,'package_writes_observed':False,'pacman_pids':sorted(active),
            'mounted_installer_targets':len(roots),
            'scope':'Read-only VM stage evidence; not a power-cut or recovery result'}
    if len(roots) != 1 or not active:return record
    db=roots[0]/'var/lib/pacman/local'
    if db.is_symlink() or not db.is_dir():return record
    samples=[]
    for package in sorted(db.iterdir())[:256]:
        if package.is_symlink() or not package.is_dir():continue
        desc=package/'desc'
        try:
            if desc.is_symlink() or not desc.is_file():continue
            size=desc.stat().st_size
        except FileNotFoundError:
            continue
        if size>0 and re.fullmatch(r'[A-Za-z0-9+_.:@-]+',package.name):
            samples.append({'package_directory':package.name,'desc_bytes':size})
        if len(samples)==3:break
    record.update(package_writes_observed=bool(samples),written_package_samples=samples)
    return record


def main():
    require_live()  # Before runtime/proc observations.
    print(json.dumps(observe()))


if __name__=='__main__':main()
