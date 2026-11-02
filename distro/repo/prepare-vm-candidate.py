#!/usr/bin/env python3
"""Owned-builder-only marker repack for R4, never a desktop release builder.

Preserves every existing program/data payload byte. Changes package metadata
and adds one explicit VM TEST marker. No downloads, install or signing here.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path,PurePosixPath
import re
import shlex
import shutil
import subprocess
import tarfile

spec=importlib.util.spec_from_file_location('repo_producer',Path(__file__).with_name('produce-test-repo.py'))
producer=importlib.util.module_from_spec(spec);spec.loader.exec_module(producer)
PROOF='usr/share/doc/platinum-reality/r4-vm-test.txt'
MARKER=b'Platinum Reality R4 VM TEST marker repack. NOT A RELEASE.\n'
METADATA={'.PKGINFO','.BUILDINFO','.MTREE'}


def inspect(package):
    """Bounded archive inventory; read as data, never extract or execute here."""
    inventory={};fields={};seen=set();total=0
    with tarfile.open(package,'r:*') as archive:
        for member in archive:
            name=member.name.removeprefix('./').rstrip('/')
            path=PurePosixPath(name)
            if (not name or path.is_absolute() or '..' in path.parts or str(path)!=name or
                    '\x00' in name or '\n' in name or name in seen):
                raise producer.Refused('Noncanonical or duplicate baseline archive path')
            seen.add(name)
            # This initial fixture supports only the current regular-file desktop
            # payload. Unknown link/special-file/install-script semantics refuse.
            if name in ('.INSTALL','.CHANGELOG') or not (member.isdir() or member.isfile()):
                raise producer.Refused('Unsupported package link, special file or install script')
            if member.isdir():
                inventory[name]={'kind':'directory','mode':member.mode};continue
            if member.size<0 or member.size>512*1024**2:raise producer.Refused('Oversized package member')
            total+=member.size
            if total>2*1024**3:raise producer.Refused('Oversized package payload')
            stream=archive.extractfile(member)
            if name=='.PKGINFO':
                if member.size>65536:raise producer.Refused('Oversized metadata')
                data=stream.read(65537)
                if len(data)!=member.size:raise producer.Refused('Truncated metadata')
                for line in data.decode('utf-8').splitlines():
                    if not line or line.startswith('#'):continue
                    key,sep,value=line.partition(' = ')
                    if not sep or not re.fullmatch(r'[a-z_]+',key):raise producer.Refused('Invalid package metadata')
                    fields.setdefault(key,[]).append(value)
            elif name not in METADATA and name!=PROOF:
                inventory[name]={'kind':'file','mode':member.mode,'bytes':member.size,
                                 'sha256':hashlib.file_digest(stream,'sha256').hexdigest()}
    if not fields:raise producer.Refused('Missing package metadata')
    return fields,inventory


def one(fields,key):
    values=fields.get(key,[])
    if len(values)!=1:raise producer.Refused('Ambiguous/missing metadata '+key)
    return values[0]


def recipe(fields,version,baseline_hash):
    if (one(fields,'pkgname')!='platinum-reality' or one(fields,'arch')!='x86_64' or
            not re.fullmatch(r'[A-Za-z0-9+_.]+',version)):
        raise producer.Refused('Expected named x86_64 desktop and safe VM-test version')
    if 'epoch' in fields or 'install' in fields:raise producer.Refused('Unsupported baseline package semantics')
    # Every copied field becomes a shell literal, never executable shell text.
    lines=['# VM TEST marker repack only; not a new native build or release.',
           'pkgname=platinum-reality','pkgver='+shlex.quote(version),'pkgrel=1',
           'arch=(x86_64)','pkgdesc='+shlex.quote(one(fields,'pkgdesc')),
           'url='+shlex.quote(one(fields,'url'))]
    mapping={'license':'license','depend':'depends','optdepend':'optdepends','provides':'provides',
             'conflict':'conflicts','replaces':'replaces','backup':'backup','group':'groups'}
    for original,variable in mapping.items():
        values=fields.get(original,[])
        if values:lines.append(variable+'=('+' '.join(shlex.quote(v) for v in values)+')')
    lines+=['options=(!debug !strip)','source=(baseline.pkg.tar.zst)','noextract=(baseline.pkg.tar.zst)',
            'sha256sums=('+baseline_hash+')','', 'package() {',
            '    bsdtar -xf "$srcdir/baseline.pkg.tar.zst" -C "$pkgdir" --exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE',
            '    install -d "$pkgdir/usr/share/doc/platinum-reality"',
            '    printf %s '+shlex.quote(MARKER.decode())+' > "$pkgdir/'+PROOF+'"','}','']
    return '\n'.join(lines)


def prepare(args):
    producer.require_builder() # Before package reads, outputs or makepkg.
    baseline,old=producer.checked_package(args.baseline,args.baseline_receipt,PROOF)
    fields,original=inspect(baseline)
    if old['proof_sha256'] is not None:raise producer.Refused('Baseline already contains the test marker')
    version=args.version
    text=recipe(fields,version,old['sha256'])
    ordered=subprocess.check_output(['/usr/bin/vercmp',version+'-1',old['pkgver']],text=True).strip()
    if int(ordered)<=0:raise producer.Refused('VM-test marker candidate must be newer')
    work=args.output.resolve()
    if not work.is_relative_to(producer.GUEST_HOME):raise producer.Refused('Owned guest home output only')
    work.mkdir(mode=0o700,exist_ok=False)
    shutil.copyfile(baseline,work/'baseline.pkg.tar.zst')
    if producer.digest(work/'baseline.pkg.tar.zst')!=old['sha256']:raise producer.Refused('Copied baseline changed')
    (work/'PKGBUILD').write_text(text)
    (work/'makepkg.conf').write_text('source /etc/makepkg.conf\nPKGDEST="$PWD/out"\nSRCDEST="$PWD"\nSRCPKGDEST="$PWD"\nLOGDEST="$PWD"\nBUILDDIR="$PWD"\n')
    env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8'}
    for key,dirname in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp')]:
        path=work/dirname;path.mkdir(mode=0o700);env[key]=str(path)
    (work/'out').mkdir()
    with (work/'makepkg.log').open('wb') as log:
        subprocess.run(['/usr/bin/makepkg','--nodeps','--nocheck','--noconfirm','--config',str(work/'makepkg.conf')],
                       cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    outputs=list((work/'out').glob('*.pkg.tar.zst'))
    if len(outputs)!=1:raise producer.Refused('Expected exactly one marker candidate package')
    package=outputs[0];new=producer.package_info(package,PROOF);new_fields,copied=inspect(package)
    if copied!=original:raise producer.Refused('Repack changed an existing payload file, mode or directory')
    if new['pkgver']!=version+'-1' or new['proof_sha256']!=hashlib.sha256(MARKER).hexdigest():
        raise producer.Refused('Candidate identity or marker differs')
    for key in ('pkgdesc','url','license','depend','optdepend','provides','conflict','replaces','backup','group'):
        if new_fields.get(key,[])!=fields.get(key,[]):raise producer.Refused('Repack changed runtime metadata '+key)
    record={'vm_test_only':True,'release_candidate':False,'kind':'VM_TEST_MARKER_REPACK',
            'baseline_package_sha256':old['sha256'],'baseline_version':old['pkgver'],
            'package_sha256':new['sha256'],'package':str(package),'version':new['pkgver'],
            'proof_file':PROOF,'proof_sha256':new['proof_sha256'],'preserved_payload_entries':len(original),
            'native_compile':'NOT RUN; exact baseline payload preserved except added VM-test marker',
            'dependency_resolution':'NOT RUN by makepkg --nodeps; actual consumer transaction still required',
            'signing':'NOT RUN','consumer_upgrade':'NOT RUN','consumer_rollback':'NOT RUN'}
    (work/'candidate-build.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--baseline-receipt',type=Path,required=True)
    parser.add_argument('--version',default='0.1.0.r4vmtest')
    parser.add_argument('--output',type=Path,required=True)
    print(json.dumps(prepare(parser.parse_args())))


if __name__=='__main__':main()
