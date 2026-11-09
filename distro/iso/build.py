#!/usr/bin/env python3
"""Build an Archiso in an owned Arch guest, never on the host."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
import shutil
from pathlib import Path
import shlex
import subprocess
import tarfile
from pathlib import PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('owned', ROOT/'distro/vm/owned.py')
owned = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owned)


def require_host_space(path, minimum):
    if shutil.disk_usage(path).free < minimum:
        raise SystemExit('Preserving host free space: need at least '+str(minimum)+' free bytes at '+str(path))


def extract_profile(archive, destination, prefix="releng"):
    # releng legitimately contains absolute systemd symlink targets. Preserve
    # those as inert source entries, but never extract a member through a link.
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        links = {str(PurePosixPath(m.name)) for m in members if m.issym()}
        names = set()
        for member in members:
            name = PurePosixPath(member.name)
            if (name.is_absolute() or '..' in name.parts or not name.parts or
                    name.parts[0] != prefix or str(name) in names or
                    not (member.isfile() or member.isdir() or member.issym()) or
                    any(str(parent) in links for parent in name.parents)):
                raise ValueError('Unsafe upstream profile archive member')
            names.add(str(name))
        destination.mkdir()
        tar.extractall(destination, filter='fully_trusted')


def resolve_source_pins(args):
    # Resolve both refs before starting/bootstraping the builder. A collaborator
    # may commit while bootstrap runs; the build must not silently change pins.
    args.source_commit = subprocess.check_output(['git', '-C', str(args.source_repo), 'rev-parse',
        '--verify', args.source_commit+'^{commit}'], text=True).strip()
    subprocess.run(['git', '-C', str(args.source_repo), 'cat-file', '-e',
                    args.source_commit+':packaging/installer/platinum-reality-installer.desktop'], check=True)
    args.distribution_commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse',
        '--verify', 'HEAD^{commit}'], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bootstrap-profile', action='store_true', help='Acquire installed releng profile and provenance; no ISO yet')
    parser.add_argument('--base', type=Path, required=True, help='Explicit self-contained Arch cloud qcow2; never a physical device')
    parser.add_argument('--source-repo', type=Path, default=ROOT, help='Native source Git repository (default: this checkout)')
    parser.add_argument('--source-commit', required=True, help='Explicit native source commit; use Sammy-approved fixed canonical pin')
    parser.add_argument('--lease-wait', type=float, default=0, help='Wait at most 1800 seconds for the shared builder/test lease')
    parser.add_argument('--date', default=datetime.now(timezone.utc).strftime('%Y.%m.%d'))
    args = parser.parse_args()
    if not 0 <= args.lease_wait <= 1800:
        parser.error('--lease-wait must be between 0 and 1800 seconds')
    if os.geteuid() == 0:
        raise SystemExit('Run as an ordinary host user. Root is used only inside the owned guest.')
    if args.bootstrap_profile and (ROOT/'distro/iso/profile').exists():
        raise SystemExit('Profile already exists; refusing to overwrite local changes.')
    if not args.bootstrap_profile:
        # Fail on stale/missing pins or collisions before starting a builder.
        resolve_source_pins(args)
        if datetime.strptime(args.date, '%Y.%m.%d').strftime('%Y.%m.%d') != args.date:
            raise ValueError('Expected an explicit YYYY.MM.DD ISO version')
        target = ROOT/'distro/iso/out'/('platinum-reality-'+args.date+'-x86_64.iso')
        if target.exists() or target.with_suffix('.iso.part').exists():
            raise FileExistsError('Existing image/partial output preserved: '+str(target))
    # A preflight floor, not a guarantee that an arbitrarily large build fits.
    # Refuse before creating or starting a builder on an already crowded host.
    require_host_space(ROOT, 20 * 1024**3)
    state = ROOT/'build/distro'
    state.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    evidence = state/(('bootstrap-' if args.bootstrap_profile else 'iso-build-') + stamp)
    evidence.mkdir()
    guest = owned.Guest(state/'builder')
    with owned.Lease(wait=args.lease_wait):
        try:
            guest.initialize_builder(args.base)
            guest.start()
            guest.wait_ssh()
            print('Owned builder ready; installing archiso INSIDE guest.', flush=True)
            with (evidence/'guest-bootstrap.log').open('wb') as log:
                guest.ssh('sudo pacman -Syu --noconfirm --needed archiso git base-devel python', stdout=log, stderr=subprocess.STDOUT, timeout=1200)
            guest.put(ROOT/'distro/iso/builder-kernel.py', '/home/tester/builder-kernel.py')
            with (evidence/'builder-kernel.json').open('wb') as log:
                guest.ssh('sudo python3 -I /home/tester/builder-kernel.py', stdout=log)
            with (evidence/'builder-storage.txt').open('wb') as log:
                guest.ssh('df -h /; lsblk', stdout=log)
            with (evidence/'archiso-version.txt').open('wb') as out:
                guest.ssh('pacman -Q archiso', stdout=out)
            if args.bootstrap_profile:
                if (ROOT/'distro/iso/profile').exists():
                    raise RuntimeError('Profile already exists; refusing to overwrite local changes')
                archive = evidence/'releng.tar'
                with archive.open('wb') as out:
                    guest.ssh('tar -C /usr/share/archiso/configs -cf - releng', stdout=out)
                extract_profile(archive, evidence/'upstream')
                (evidence/'upstream/releng').rename(ROOT/'distro/iso/profile')
                version = (evidence/'archiso-version.txt').read_text().strip()
                (ROOT/'distro/iso/UPSTREAM.md').write_text(
                    '# Archiso profile provenance\n\n'
                    f'- Package installed inside the copied Arch guest: `{version}`.\n'
                    f'- Copy UTC: `{stamp}`.\n'
                    '- Source: `/usr/share/archiso/configs/releng/`, copied with tar.\n'
                    f'- Unmodified acquisition tar SHA256: `{owned.sha256(archive)}`.\n'
                    '- Upstream: https://gitlab.archlinux.org/archlinux/archiso\n'
                    '- Profile license: upstream GPL-3.0-or-later; see the installed package.\n'
                    '- Local changes and package/boot provenance are documented in README.md.\n')
                print('Installed releng profile acquired with provenance. No ISO built.', flush=True)
            else:
                build_iso(args, guest, evidence)
            (evidence/'result.json').write_text(json.dumps({'ok': True, 'phase': 'bootstrap-profile' if args.bootstrap_profile else 'build'}, indent=2)+'\n')
        except BaseException as error:
            (evidence/'result.json').write_text(json.dumps({'ok': False, 'error': str(error)}, indent=2)+'\n')
            raise
        finally:
            guest.stop()
            (evidence/'lease-finished.txt').write_text(datetime.now(timezone.utc).isoformat()+'\n')


def build_iso(args, guest, evidence):
    if not (ROOT/'distro/iso/UPSTREAM.md').is_file():
        raise SystemExit('Acquire the upstream profile with --bootstrap-profile first.')
    # Resolve and export the named commit, never the working tree (or user data).
    commit = args.source_commit
    epoch = int(subprocess.check_output(['git', '-C', str(args.source_repo), 'show', '-s',
                                        '--format=%ct', commit], text=True).strip())
    if datetime.strptime(args.date, '%Y.%m.%d').strftime('%Y.%m.%d') != args.date:
        raise ValueError('Expected an explicit YYYY.MM.DD ISO version')
    archive = evidence/'source.tar'
    owned.run(['git', '-C', args.source_repo, 'archive', '--format=tar', '-o', archive, commit])
    pin = {'commit': commit, 'source_date_epoch': epoch, 'iso_date': args.date,
           'git_archive_sha256': owned.sha256(archive)}
    (evidence/'pin.json').write_text(json.dumps(pin, indent=2)+'\n')
    # Distribution overlay is another exact commit, never the mutable working
    # tree or files imported from a user's desktop/configuration.
    distribution = args.distribution_commit
    overlay = evidence/'distribution.tar'
    owned.run(['git', '-C', ROOT, 'archive', '--format=tar', '-o', overlay, distribution, 'distro'])
    extract_profile(overlay, evidence/'distribution-source', prefix='distro')
    distribution_root = evidence/'distribution-source/distro'
    profile = distribution_root/'iso/profile'
    engine = distribution_root/'installer-engine/engine.py'
    wrapper = profile/'airootfs/usr/bin/platinum-reality-install'
    if not engine.is_file() or not wrapper.is_file():
        raise ValueError('Commit the live-engine distribution source before building an installer ISO')
    destination = profile/'airootfs/usr/lib/platinum-reality/installer-engine'
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(engine, destination)
    pin.update(distribution_commit=distribution, distribution_archive_sha256=owned.sha256(overlay),
               engine_sha256=owned.sha256(engine), wrapper_sha256=owned.sha256(wrapper))
    profile_tar = evidence/'profile.tar'
    with tarfile.open(profile_tar, 'w') as tar:
        tar.add(profile, arcname='profile')
    pin['profile_tar_sha256'] = owned.sha256(profile_tar)
    (evidence/'pin.json').write_text(json.dumps(pin, indent=2)+'\n')
    (evidence/'profile-transport.sha256').write_text(pin['profile_tar_sha256']+'  profile.tar\n')
    remote = '/home/tester/iso-' + evidence.name
    guest.ssh('mkdir '+shlex.quote(remote))
    for source in (archive, profile_tar, evidence/'pin.json', distribution_root/'iso/guest-build.py', distribution_root/'iso/guest-root.py'):
        guest.put(source, remote+'/'+source.name)
    guest.ssh('tar -xf '+shlex.quote(remote+'/profile.tar')+' -C '+shlex.quote(remote))
    print(f'Building native commit {commit} and the ISO in the owned guest. Log: {evidence}/guest-build.log', flush=True)
    with (evidence/'guest-build.log').open('wb') as log:
        guest.ssh('python3 -I '+shlex.quote(remote+'/guest-build.py')+' '+shlex.quote(remote),
                  stdout=log, stderr=subprocess.STDOUT, timeout=10800)
    for name in ('pin.json', 'provenance.json', 'build-receipt.json', 'native-build.json', 'source-manifest.json', 'PKGBUILD'):
        with (evidence/name).open('wb') as out:
            guest.ssh('cat '+shlex.quote(remote+'/'+name), stdout=out)
    receipt = json.loads((evidence/'build-receipt.json').read_text())
    expected_name = 'platinum-reality-'+args.date+'-x86_64.iso'
    if receipt['iso'] != expected_name:
        raise RuntimeError('Unexpected ISO name from builder')
    output = ROOT/'distro/iso/out'
    output.mkdir(exist_ok=True)
    target = output/expected_name
    if target.exists():
        raise FileExistsError('Existing ISO preserved: '+str(target))
    # Guest work may have consumed space since preflight. Leave a host reserve
    # after transport rather than filling its filesystem with a partial ISO.
    require_host_space(output, receipt['size_bytes'] + 8 * 1024**3)
    partial = target.with_suffix('.iso.part')
    with partial.open('xb') as out:
        guest.ssh('cat '+shlex.quote(remote+'/out/'+expected_name), stdout=out, timeout=600)
    if partial.stat().st_size != receipt['size_bytes'] or owned.sha256(partial) != receipt['sha256']:
        raise RuntimeError('ISO transport verification failed; partial file retained')
    partial.rename(target)
    (output/(expected_name+'.sha256')).write_text(receipt['sha256']+'  '+expected_name+'\n')
    print('ISO built and SHA256 verified: '+str(target), flush=True)
    print('Boot acceptance is separate; no boot success claimed by the build.', flush=True)


if __name__ == '__main__':
    main()
