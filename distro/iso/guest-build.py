#!/usr/bin/env python3
"""Runs ONLY as tester in the copied pr-iso-builder VM, with guest-only sudo."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess


def run(args, **kwargs):
    print('+', ' '.join(str(x) for x in args), flush=True)
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    if os.geteuid() == 0 or socket.gethostname() != 'pr-iso-builder':
        raise SystemExit('Requires the unprivileged user in the owned ISO builder.')
    virtual = subprocess.check_output(['systemd-detect-virt', '--vm'], text=True).strip()
    if virtual not in ('qemu', 'kvm'):
        raise SystemExit('Refusing a non-QEMU builder.')
    work = args.directory.resolve(strict=True)
    pin = json.loads((work/'pin.json').read_text())
    environment = dict(os.environ, SOURCE_DATE_EPOCH=str(pin['source_date_epoch']),
                       CMAKE_BUILD_PARALLEL_LEVEL='4', CARGO_BUILD_JOBS='4', MAKEFLAGS='-j4')
    native = Path('/home/tester')/('native-' + pin['commit'])
    if not native.exists():
        native.mkdir()
        # git archive contains only tracked files from the named canonical commit.
        if digest(work/'source.tar') != pin['git_archive_sha256']:
            raise RuntimeError('Native source transport hash mismatch')
        run(['tar', '-xf', work/'source.tar', '-C', native])
        run(['python3', '-I', native/'scripts/package-arch.py', '--output', native/'package'])
        (native/'pin.json').write_text(json.dumps(pin, indent=2)+'\n')
    elif json.loads((native/'pin.json').read_text())['git_archive_sha256'] != pin['git_archive_sha256']:
        raise RuntimeError('Cached native snapshot has a different source hash')
    # The release version comes from the source's VERSION file (update 44 is 0.44).
    version = (native/'VERSION').read_text().strip()
    package = native/f'package/platinum-reality-{version}-1-x86_64.pkg.tar.zst'
    build_receipt = native/'native-build.json'
    # A package without a successful makepkg receipt (an interrupted build) is rebuilt over.
    if not package.exists() or not build_receipt.is_file():
        run(['makepkg', '--syncdeps', '--noconfirm', '--force', '--config', 'makepkg.conf'],
            cwd=native/'package', env=environment)
        build_receipt.write_text(json.dumps({'commit': pin['commit'], 'package_sha256': digest(package),
            'makepkg_check': 'passed', 'source_archive_sha256': pin['git_archive_sha256']}, indent=2)+'\n')
    if (not build_receipt.is_file() or
            json.loads(build_receipt.read_text())['package_sha256'] != digest(package)):
        raise RuntimeError('Cached package has no matching successful makepkg receipt')
    shutil.copy2(build_receipt, work/'native-build.json')
    run(['sudo', 'pacman', '-S', '--noconfirm', '--needed', 'archiso', 'plymouth'])
    # Generate only the original bundled icon; never inspect/import a user's data.
    empty = work/'empty-data'
    empty.mkdir(exist_ok=True)
    splash = work/'splash'
    run(['python3', '-I', native/'scripts/boot-splash.py', 'render', '--size', '1024x768',
         '--rotate', '0', '--output', splash], env=dict(environment, XDG_DATA_HOME=str(empty)))
    if json.loads((splash/'manifest.json').read_text())['icon'] != 'original':
        raise RuntimeError('Refusing non-original splash assets')
    profile = work/'profile'
    if digest(work/'profile.tar') != pin['profile_tar_sha256']:
        raise RuntimeError('Live profile transport hash mismatch')
    engine = profile/'airootfs/usr/lib/platinum-reality/installer-engine'
    wrapper = profile/'airootfs/usr/bin/platinum-reality-install'
    if digest(engine) != pin['engine_sha256'] or digest(wrapper) != pin['wrapper_sha256']:
        raise RuntimeError('Live engine/wrapper source mismatch')
    # Copy only the native pin's tracked, shipped launcher. This lives on the
    # live user's desktop, never in the installed target's application menu.
    launcher = native/'packaging/installer/platinum-reality-installer.desktop'
    if not launcher.is_file():
        raise RuntimeError('Native pin predates the real installer GUI')
    desktop = profile/'airootfs/home/liveuser/Desktop'
    desktop.mkdir(parents=True)
    shutil.copyfile(launcher, desktop/'Install Platinum Reality.desktop')
    theme = profile/'airootfs/usr/share/plymouth/themes/platinum-reality'
    # Use the native pin's complete generator: the template has dialog,
    # rotation, scale and bullet substitutions beyond the original four.
    run(['python3', '-I', native/'scripts/boot-splash.py', 'theme', '--from', splash,
         '--output', theme], env=environment)
    if '@' in (theme/'platinum-reality.script').read_text():
        raise RuntimeError('Unfilled placeholder in the generated live splash theme')
    # The frozen helper's RENDERED[:-1] drops bullet-8.png, not the manifest.
    # Complete the image without modifying that native pin's source.
    spec = importlib.util.spec_from_file_location('pinned_boot_splash', native/'scripts/boot-splash.py')
    helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
    helper.checked_rendering(splash)
    for name in helper.RENDERED:
        shutil.copyfile(splash/name, theme/name)
    # Live-only installer helper; preserve its source-relative template layout.
    # Rendering already ran as tester with empty XDG data and original artwork.
    boot_assets = profile/'airootfs/usr/lib/platinum-reality/installer-boot'
    sources = ['scripts/boot-splash.py', 'packaging/plymouth/platinum-reality.plymouth',
               'packaging/plymouth/platinum-reality.script.in']
    asset_hashes = {}
    for name in sources:
        destination = boot_assets/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(native/name, destination)
        asset_hashes[name] = digest(destination)
    (boot_assets/'sha256.json').write_text(json.dumps(asset_hashes, sort_keys=True)+'\n')
    shutil.copy2(native/'package/source-manifest.json', work/'source-manifest.json')
    shutil.copy2(native/'package/PKGBUILD', work/'PKGBUILD')
    # Package construction is unprivileged; root image/repo operations are a
    # separately guarded guest-only script.
    run(['sudo', 'python3', work/'guest-root.py', work, package], env=environment)


if __name__ == '__main__':
    main()
