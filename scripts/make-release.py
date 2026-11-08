#!/usr/bin/env python3
"""Build, sign and stage a Platinum Reality release on potato (docs/software-update.md).

  make-release.py --notes notes.txt [--copy-to /run/media/<user>/External/platinum-releases]

Makes build/releases/<version>-<rel>/ holding the package, release.json and their signatures,
checks it exactly as Software Update will, and optionally copies it to a disk (the external SSD)
and compares the copy. Publishing to GitHub is a separate, deliberate step that needs psn_tch's go:
this script never contacts GitHub.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

project = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(project / 'scripts'))
import platinum_update  # noqa: E402

KEY_HOME = Path.home() / '.local/share/platinum-reality-release/gnupg'
VALID = timedelta(days=180)


def sign(path, key_home):
    signature = Path(str(path) + '.sig')
    signature.unlink(missing_ok=True)
    subprocess.run(['gpg', '--homedir', str(key_home), '--batch', '--pinentry-mode', 'loopback', '--passphrase', '',
                    '--detach-sign', '-o', str(signature), str(path)], check=True, capture_output=True)


def build(work):
    """The package, built from a pinned source snapshot with makepkg (its check() runs the tests)."""
    subprocess.run([sys.executable, '-I', str(project / 'scripts/package-arch.py'), '--output', str(work)], check=True)
    environment = {key: os.environ[key] for key in ('HOME', 'USER', 'LANG', 'PATH') if key in os.environ}
    environment['MAKEPKG_CONF'] = str(work / 'makepkg.conf')
    subprocess.run(['dbus-run-session', '--', 'makepkg', '--cleanbuild', '--noconfirm', '--config',
                    str(work / 'makepkg.conf')], cwd=work, env=environment, check=True)
    packages = sorted(p for p in work.glob('platinum-reality-*-x86_64.pkg.tar.zst') if '-debug-' not in p.name)
    if len(packages) != 1:
        raise SystemExit(f'Expected one package in {work}, found {len(packages)}')
    return packages[0]


def stage(package, notes, folder, key_home, now):
    version = package.name[len('platinum-reality-'):-len('-x86_64.pkg.tar.zst')]
    folder.mkdir(parents=True, exist_ok=False)
    staged = folder / package.name
    shutil.copy2(package, staged)
    sign(staged, key_home)
    data = staged.read_bytes()
    release = {'format': 1, 'name': platinum_update.NAME, 'version': version,
               'date': now.strftime('%Y-%m-%dT%H:%M:%SZ'), 'valid_until': (now + VALID).strftime('%Y-%m-%dT%H:%M:%SZ'),
               'arch': platinum_update.ARCH, 'package': package.name,
               'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'notes': notes.strip()}
    (folder / platinum_update.RELEASE).write_text(json.dumps(release, indent=2) + '\n')
    sign(folder / platinum_update.RELEASE, key_home)
    return release


def copy(folder, destination):
    target = destination / folder.name
    if target.exists():
        raise SystemExit(f'{target} already exists; nothing was copied')
    shutil.copytree(folder, target)
    for path in folder.iterdir():
        if (target / path.name).read_bytes() != path.read_bytes():
            raise SystemExit(f'The copy of {path.name} on {destination} differs from the original')
    subprocess.run(['sync', '-f', str(target)], check=False)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--notes', type=Path, required=True, help='plain-text release notes')
    parser.add_argument('--package', type=Path, help='an already built package (skips makepkg)')
    parser.add_argument('--copy-to', type=Path, help='also copy the release folder here and compare it')
    parser.add_argument('--key-home', type=Path, default=KEY_HOME, help='GnuPG home holding the release key')
    parser.add_argument('--keyring', type=Path, default=project / 'packaging/release-keys.gpg',
                        help='public keyring the release must verify against')
    parser.add_argument('--output', type=Path, default=project / 'build/releases')
    options = parser.parse_args()
    notes = options.notes.read_text()
    now = datetime.now(timezone.utc)
    stamp = now.strftime('%Y%m%dT%H%M%SZ')
    if options.package:
        package = options.package.resolve(strict=True)
    else:
        package = build(options.output / 'work' / stamp)
    version = package.name[len('platinum-reality-'):-len('-x86_64.pkg.tar.zst')]
    folder = options.output / f'platinum-reality-{version}'
    release = stage(package, notes, folder, options.key_home, now)
    # Check it exactly as Software Update will, against the public keyring the package ships.
    platinum_update.verify_bundle(folder, options.keyring, installed=None, now=now)
    print(json.dumps({'release': str(folder), 'version': release['version'], 'sha256': release['sha256']}))
    if options.copy_to:
        target = copy(folder, options.copy_to)
        platinum_update.verify_bundle(target, options.keyring, installed=None, now=now)
        print(json.dumps({'copied': str(target)}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
