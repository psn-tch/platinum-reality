#!/usr/bin/env python3
"""Guest-root archiso work. No API for arbitrary disks, mounts or host execution."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time


def run(args, **kwargs):
    print('+', ' '.join(str(x) for x in args), flush=True)
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    if os.geteuid() != 0 or socket.gethostname() != 'pr-iso-builder':
        raise SystemExit('Guest root in pr-iso-builder is required; never run on a host.')
    virtual = subprocess.check_output(['systemd-detect-virt', '--vm'], text=True).strip()
    if virtual not in ('qemu', 'kvm'):
        raise SystemExit('Refusing a non-QEMU builder.')
    work, package = (Path(value).resolve(strict=True) for value in sys.argv[1:])
    if not work.is_relative_to('/home/tester') or not package.is_relative_to('/home/tester'):
        raise SystemExit('All inputs must be inside the owned guest account.')
    pin = json.loads((work/'pin.json').read_text())
    environment = dict(os.environ, SOURCE_DATE_EPOCH=str(pin['source_date_epoch']))
    profile = work/'profile'
    repo = profile/'airootfs/opt/platinum-reality/repo'
    repo.mkdir(parents=True)
    shutil.copy2(package, repo/package.name)
    db = repo/'platinum-reality.db.tar.gz'
    run(['repo-add', db, repo/package.name], env=environment)
    local = f'\n[platinum-reality]\nSigLevel = Optional TrustAll\nServer = file://{repo}\n\n'
    upstream = (profile/'pacman.conf').read_text()
    # Custom repo BEFORE official repos; signature policy is scoped to this
    # unsigned prototype repo, not official package downloads.
    config = upstream.replace('[core]', local+'[core]', 1)
    (profile/'pacman.conf').write_text(config)
    (profile/'airootfs/etc/pacman.conf').write_text(upstream.replace(
        '[core]', local.replace(str(repo), '/opt/platinum-reality/repo')+'[core]', 1))
    # Empty installed-package DB is essential: -Sw must include the entire
    # closure, even dependencies already present in the build guest.
    download_db = work/'download-db'
    download_db.mkdir()
    targets = (profile/'packages.x86_64').read_text().split()
    if any(name.lower() == 'networkmanager' for name in targets):
        raise RuntimeError('NetworkManager is outside the ISO contract')
    run(['pacman', '--config', profile/'pacman.conf', '--dbpath', download_db,
         '--cachedir', repo, '-Syw', '--noconfirm', *targets])
    packages = sorted(repo.glob('*.pkg.tar.zst'))
    run(['repo-add', db, *packages], env=environment)
    inventory = []
    for path in packages:
        with path.open('rb') as stream:
            sha = hashlib.file_digest(stream, 'sha256').hexdigest()
        inventory.append({'file': path.name, 'size_bytes': path.stat().st_size, 'sha256': sha})
    record = {'native_source': pin, 'archiso': subprocess.check_output(['pacman', '-Q', 'archiso'], text=True).strip(),
              'repo_signing': 'UNSIGNED R1 prototype; local repo Optional TrustAll only',
              'packages': inventory, 'assets': 'Original project splash only; no chime or user imports.'}
    (profile/'airootfs/opt/platinum-reality/image.json').write_text(json.dumps({
        'version': 1, 'native_source': pin, 'package_sha256': digest(package),
        'scope': 'VM-only unsigned prototype; live engine and passwordless wrapper are not target packages.'}, indent=2)+'\n')
    (repo/'provenance.json').write_text(json.dumps(record, indent=2)+'\n')
    shutil.copy2(repo/'provenance.json', work/'provenance.json')
    # Rebrand date is an explicit input, rather than a clock read in profiledef.
    with (profile/'profiledef.sh').open('a') as out:
        out.write('\niso_version="'+pin['iso_date']+'"\n')
    start = time.monotonic()
    run(['mkarchiso', '-v', '-w', work/'archiso-work', '-o', work/'out', profile], env=environment)
    iso, = (work/'out').glob('*.iso')
    with iso.open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    (work/'out'/f'{iso.name}.sha256').write_text(f'{sha}  {iso.name}\n')
    (work/'build-receipt.json').write_text(json.dumps({
        'iso': iso.name, 'sha256': sha, 'size_bytes': iso.stat().st_size,
        'mkarchiso_seconds': round(time.monotonic()-start, 3), 'native_source': pin,
        'uefi_boot': 'NOT TESTED YET', 'bios_boot': 'NOT TESTED'}, indent=2)+'\n')


if __name__ == '__main__':
    main()
