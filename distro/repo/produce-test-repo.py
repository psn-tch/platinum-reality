#!/usr/bin/env python3
"""Source candidate: sign two package snapshots ONLY in the owned builder VM.

NOT a release signer. Never run on a host, use a real key or publish its output.
The only transferable output is public/; signing material stays in private/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import socket
import subprocess
import tarfile

IDENTITY = 'Platinum Reality VM TEST KEY - NOT FOR RELEASE <vm-test@example.invalid>'
DATABASE = 'platinum-reality-vm-test.db.tar.gz'
GUEST_HOME = Path('/home/tester')


class Refused(ValueError):
    pass


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def require_builder():
    if os.geteuid() == 0 or socket.gethostname() != 'pr-iso-builder':
        raise Refused('Unprivileged owned builder VM only; no host or release keys')
    virtual = subprocess.run(['/usr/bin/systemd-detect-virt', '--vm'], capture_output=True,
                             text=True, check=False, timeout=5)
    if virtual.returncode or virtual.stdout.strip() not in ('qemu', 'kvm'):
        raise Refused('QEMU/KVM is required')
    # --vm deliberately ignores a container layer; check it independently.
    container = subprocess.run(['/usr/bin/systemd-detect-virt', '--container'], capture_output=True,
                               text=True, check=False, timeout=5)
    if container.returncode != 1:
        raise Refused('Containers or an indeterminate container check are not the owned builder environment')


def checked_path(path):
    if path.is_symlink() or not path.is_file():
        raise Refused('Inputs must be regular files, never symlinks')
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(GUEST_HOME):
        raise Refused('All inputs must stay inside the owned guest home')
    return resolved


def proof_name(value):
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or '..' in path.parts or str(path) != value or
            not value.startswith('usr/') or '\n' in value or '\x00' in value):
        raise Refused('Proof file must be one canonical relative usr/ payload path')
    return value


def package_info(package, proof):
    # Reads two archive members as data; never extracts or executes a package.
    if not re.fullmatch(r'[A-Za-z0-9+_.:-]+\.pkg\.tar\.zst', package.name):
        raise Refused('Expected a normal Arch package filename')
    fields = {}; payload = None; seen = set()
    with tarfile.open(package, 'r:*') as archive:
        for member in archive:
            name = member.name.removeprefix('./')
            if name not in ('.PKGINFO', proof):
                continue
            if name in seen or not member.isfile() or member.issym() or member.islnk():
                raise Refused('Duplicate or non-regular package metadata/proof')
            seen.add(name)
            limit = 65536 if name == '.PKGINFO' else 64*1024**2
            if member.size > limit:
                raise Refused('Package metadata/proof is too large')
            data = archive.extractfile(member).read(limit+1)
            if len(data) != member.size:
                raise Refused('Truncated package metadata/proof')
            if name == proof:
                payload = hashlib.sha256(data).hexdigest()
            else:
                for line in data.decode('utf-8').splitlines():
                    if not line or line.startswith('#'):
                        continue
                    key, separator, value = line.partition(' = ')
                    if key not in ('pkgname', 'pkgver', 'arch'):
                        continue
                    if not separator or key in fields:
                        raise Refused('Ambiguous package identity')
                    fields[key] = value
    if (fields.keys() != {'pkgname','pkgver','arch'} or fields['pkgname'] != 'platinum-reality' or
            fields['arch'] != 'x86_64' or not re.fullmatch(r'[A-Za-z0-9+_.:-]+', fields['pkgver'])):
        raise Refused('Only the named x86_64 desktop package is accepted')
    return fields | {'sha256': digest(package), 'proof_sha256': payload}


def checked_package(package, receipt, proof):
    package = checked_path(package); receipt = checked_path(receipt)
    if receipt.stat().st_size > 1024**2:
        raise Refused('Build receipt is too large')
    record = json.loads(receipt.read_text())
    info = package_info(package, proof)
    if record.get('package_sha256') != info['sha256']:
        raise Refused('Package does not match its explicit build receipt')
    return package, info


def signing_fingerprint(colons):
    fingerprints = []
    primary = False
    for line in colons.splitlines():
        fields = line.split(':')
        if fields[0] in ('sec','ssb'):
            primary = fields[0] == 'sec'
        elif fields[0] == 'fpr' and primary:
            if len(fields) < 10 or not re.fullmatch(r'(?:[0-9A-F]{40}|[0-9A-F]{64})', fields[9]):
                raise Refused('Invalid signing fingerprint')
            fingerprints.append(fields[9]); primary = False
    if len(fingerprints) != 1:
        raise Refused('Expected exactly one newly generated VM test key')
    return fingerprints[0]


def public_inventory(public):
    result = {}
    for path in sorted(public.rglob('*')):
        if path.is_symlink():
            destination = os.readlink(path)
            if '/' in destination or destination in ('.','..') or not (path.parent/destination).is_file():
                raise Refused('Unexpected public repository symlink')
            result[str(path.relative_to(public))] = {'type':'symlink','target':destination}
        elif path.is_file():
            result[str(path.relative_to(public))] = {'type':'file','bytes':path.stat().st_size,'sha256':digest(path)}
        elif not path.is_dir():
            raise Refused('Unexpected public output type')
    return result


def consumer_config(snapshot):
    if snapshot not in ('baseline', 'candidate'):
        raise Refused('Only an explicit VM-test snapshot is accepted')
    # Render only. No API writes this to a live/host configuration file.
    return ('[options]\nArchitecture = auto\nSigLevel = Required TrustedOnly\n'
            'LocalFileSigLevel = Required TrustedOnly\nRemoteFileSigLevel = Required TrustedOnly\n'
            'GPGDir = /etc/pacman.d/gnupg-r4-vm-test\n'
            'CacheDir = /var/cache/pacman/r4-vm-test\nDBPath = /var/lib/pacman\n\n'
            '[platinum-reality-vm-test]\nSigLevel = Required TrustedOnly\n'
            'Server = file:///opt/platinum-reality-r4-vm-test/snapshots/'+snapshot+'\n')


def produce(args):
    require_builder()  # Before reading files, creating directories or starting GPG.
    proof = proof_name(args.proof_file)
    baseline, old = checked_package(args.baseline, args.baseline_receipt, proof)
    candidate, new = checked_package(args.candidate, args.candidate_receipt, proof)
    if new['proof_sha256'] is None or new['proof_sha256'] == old['proof_sha256']:
        raise Refused('A higher version alone is insufficient: the installed proof file must change')
    order = subprocess.check_output(['/usr/bin/vercmp',new['pkgver'],old['pkgver']], text=True).strip()
    if int(order) <= 0:
        raise Refused('Candidate must be newer than baseline')
    work = args.output.resolve()
    if not work.is_relative_to(GUEST_HOME):
        raise Refused('Output must stay in the owned guest home')
    work.mkdir(mode=0o700, exist_ok=False)
    private = work/'private'; private.mkdir(mode=0o700)
    public = work/'public'; public.mkdir(mode=0o755)
    env = {'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','HOME':str(work),'GNUPGHOME':str(private)}
    def run(argv):
        return subprocess.run([str(x) for x in argv], env=env, capture_output=True, text=True,
                              check=True, timeout=180)
    gpg = ['gpg','--no-options','--homedir',private,'--batch','--yes']
    try:
        # Deliberately unprotected, expiring *test* key, inside this disposable VM.
        run([*gpg,'--pinentry-mode','loopback','--passphrase','',
             '--quick-generate-key',IDENTITY,'ed25519','sign','1d'])
        fingerprint = signing_fingerprint(run([*gpg,'--with-colons','--fingerprint','--list-secret-keys']).stdout)
        exported = run([*gpg,'--armor','--export',fingerprint]).stdout
        if not exported.startswith('-----BEGIN PGP PUBLIC KEY BLOCK-----') or 'PRIVATE KEY' in exported:
            raise Refused('Expected public test key export only')
        (public/'VM-TEST-KEY.asc').write_text(exported)
        snapshots = {}
        for name in ('baseline','candidate'):
            (public/('pacman-'+name+'.conf')).write_text(consumer_config(name))
        for name, source, info in [('baseline',baseline,old),('candidate',candidate,new)]:
            folder = public/'snapshots'/name; folder.mkdir(parents=True)
            package = folder/source.name; shutil.copyfile(source,package)
            if digest(package) != info['sha256']:
                raise Refused('Package copy changed')
            signature = package.with_name(package.name+'.sig')
            run([*gpg,'--local-user',fingerprint,'--output',signature,'--detach-sign',package])
            database = folder/DATABASE
            run(['repo-add','--nocolor','--sign','--key',fingerprint,database,package])
            run([*gpg,'--verify',signature,package])
            run([*gpg,'--verify',str(database)+'.sig',database])
            snapshots[name] = info
        record = {'vm_test_only':True,'release_signer':False,'fingerprint':fingerprint,
                  'private_key_exported':False,'proof_file':proof,'snapshots':snapshots,
                  'public_inventory':public_inventory(public),
                  'consumer_update':'NOT RUN','consumer_rollback':'NOT RUN',
                  'note':'Only public/ may be transferred. Never copy private/ or publish this test repository.'}
        (public/'VM-TEST-PROVENANCE.json').write_text(json.dumps(record,indent=2)+'\n')
        return record
    finally:
        # Only this keyring's agent, never the guest user's normal agent.
        subprocess.run(['gpgconf','--homedir',str(private),'--kill','gpg-agent'],env=env,
                       capture_output=True,timeout=30,check=False)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--baseline-receipt',type=Path,required=True)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--candidate-receipt',type=Path,required=True)
    parser.add_argument('--proof-file',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    record=produce(args)
    print(json.dumps({key:record[key] for key in ('vm_test_only','fingerprint','private_key_exported','consumer_update','consumer_rollback')}))


if __name__=='__main__':
    main()
