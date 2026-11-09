"""Platinum Software Update: release bundles, their verification, and the installed version.

Shared by the user's updater (checking, downloading) and the root install helper. Nothing here
changes the system; see docs/software-update.md for the release format and the trust rules.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

NAME = 'platinum-reality'
ARCH = 'x86_64'
RELEASE = 'release.json'
MAX_METADATA = 256 * 1024
MAX_PACKAGE = 1024 * 1024 * 1024
# The release keyring shipped in the installed package; tests and the release script pass another.
KEYRING = Path('/usr/share/platinum-reality/release-keys.gpg')


class UpdateError(Exception):
    """A release that must not be installed, with the reason shown to the user."""


def vercmp(a, b):
    """pacman's version comparison: negative, zero or positive."""
    return int(subprocess.run(['vercmp', a, b], capture_output=True, text=True, check=True).stdout.strip())


def installed_version(name=NAME):
    """The installed package's version from pacman's database, or None."""
    result = subprocess.run(['pacman', '-Q', name], capture_output=True, text=True)
    if result.returncode != 0:
        return None
    parts = result.stdout.split()
    return parts[1] if len(parts) == 2 and parts[0] == name else None


def gpg_verify(keyring, data, signature):
    """True only for a good signature on `data` by a key in `keyring` (gpgv, no user keyring)."""
    if not Path(keyring).is_file():
        return False
    result = subprocess.run(['gpgv', '--keyring', str(keyring), '--status-fd', '1', str(signature), str(data)],
                            capture_output=True, text=True, env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'})
    return result.returncode == 0 and any(line.startswith('[GNUPG:] VALIDSIG ') for line in result.stdout.splitlines())


def regular_file(path, limit):
    """`path` if it is a plain file (not a link) no larger than `limit`, else an UpdateError."""
    try:
        status = os.lstat(path)
    except OSError:
        raise UpdateError(f'The release is missing {Path(path).name}.')
    if not (status.st_mode & 0o170000) == 0o100000:
        raise UpdateError(f'{Path(path).name} is not a plain file.')
    if status.st_size > limit:
        raise UpdateError(f'{Path(path).name} is too large.')
    return Path(path)


def parse_time(text):
    try:
        moment = datetime.fromisoformat(str(text).replace('Z', '+00:00'))
    except ValueError:
        raise UpdateError('The release description has an unreadable date.')
    if moment.tzinfo is None:
        raise UpdateError('The release description has a date without a time zone.')
    return moment


def read_release(folder, keyring=KEYRING):
    """The verified release description in `folder`."""
    folder = Path(folder)
    metadata = regular_file(folder / RELEASE, MAX_METADATA)
    signature = regular_file(folder / (RELEASE + '.sig'), 64 * 1024)
    if not gpg_verify(keyring, metadata, signature):
        raise UpdateError('The release description is not signed by the Platinum Reality release key.')
    try:
        release = json.loads(metadata.read_text())
    except (ValueError, UnicodeDecodeError):
        raise UpdateError('The release description could not be read.')
    if not isinstance(release, dict) or release.get('format') != 1:
        raise UpdateError('This release needs a newer Software Update.')
    for key in ('name', 'version', 'date', 'valid_until', 'arch', 'package', 'sha256', 'size', 'notes'):
        if key not in release:
            raise UpdateError(f'The release description has no "{key}".')
    if release['name'] != NAME:
        raise UpdateError('This is not a Platinum Reality release.')
    if release['arch'] != ARCH:
        raise UpdateError(f'This release is for {release["arch"]} computers.')
    version = str(release['version'])
    if not re.fullmatch(r'[0-9][0-9A-Za-z.+_]*-[0-9]+', version):
        raise UpdateError('The release has an unreadable version.')
    expected = f'{NAME}-{version}-{ARCH}.pkg.tar.zst'
    if release['package'] != expected:
        raise UpdateError('The release names an unexpected package file.')
    if not re.fullmatch(r'[0-9a-f]{64}', str(release['sha256'])) or not isinstance(release['size'], int):
        raise UpdateError('The release description has no usable checksum.')
    parse_time(release['date'])
    return release


def verify_bundle(folder, keyring=KEYRING, installed=None, now=None, allow_same=False):
    """Check a release folder completely; returns (release, package path) or raises UpdateError.

    `installed` is the installed version (None when Platinum Reality is not installed), `now` the
    current time (tests pass one). Newer only, unless `allow_same` (reinstalling the same version)."""
    folder = Path(folder)
    release = read_release(folder, keyring)
    now = now or datetime.now(timezone.utc)
    if parse_time(release['valid_until']) < now:
        raise UpdateError('This release description has expired. Check for updates again.')
    package = regular_file(folder / release['package'], MAX_PACKAGE)
    signature = regular_file(folder / (release['package'] + '.sig'), 64 * 1024)
    if package.stat().st_size != release['size']:
        raise UpdateError('The package is not the size the release says.')
    digest = hashlib.sha256()
    with package.open('rb') as data:
        for block in iter(lambda: data.read(1 << 20), b''):
            digest.update(block)
    if digest.hexdigest() != release['sha256']:
        raise UpdateError('The package is damaged or was changed: its checksum does not match.')
    if not gpg_verify(keyring, package, signature):
        raise UpdateError('The package is not signed by the Platinum Reality release key.')
    if installed is not None:
        order = vercmp(release['version'], installed)
        if order < 0 or (order == 0 and not allow_same):
            raise UpdateError(f'Platinum Reality {installed} is installed; {release["version"]} is not newer.')
    return release, package


def missing_dependencies(package):
    """Dependencies of `package` that are not installed (pacman -T); empty when it can install
    without fetching anything, which is the only case the updater allows."""
    info = subprocess.run(['pacman', '-Qip', str(package)], capture_output=True, text=True,
                          env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'})
    if info.returncode != 0:
        raise UpdateError('pacman could not read the package.')
    depends, collecting = [], False
    for line in info.stdout.splitlines():
        if line.startswith('Depends On'):
            collecting = True
            line = line.split(':', 1)[1]
        elif collecting and not line.startswith(' '):
            break
        if collecting:
            depends += [d for d in line.split() if d != 'None']
    if not depends:
        return []
    missing = subprocess.run(['pacman', '-T', *depends], capture_output=True, text=True)
    return [d for d in missing.stdout.split() if d]


# ---- Installing (root only; see scripts/platinum-update-install) ----

STATE = Path('/var/cache/platinum-update')
FILES = 4  # release.json, its signature, the package and its signature


def stage(source, staging):
    """Copy a release folder's four files into `staging` (a new root-owned folder) without following
    links, so nothing can change them between checking and installing. Returns the staged folder."""
    source = Path(source)
    staging.mkdir(mode=0o700, parents=True, exist_ok=False)
    metadata = regular_file(source / RELEASE, MAX_METADATA)
    try:
        package = json.loads(metadata.read_text()).get('package', '')
    except (ValueError, UnicodeDecodeError, AttributeError):
        raise UpdateError('The release description could not be read.')
    if not isinstance(package, str) or not re.fullmatch(rf'{NAME}-[0-9A-Za-z.+_]+-[0-9]+-{ARCH}\.pkg\.tar\.zst', package):
        raise UpdateError('The release names an unexpected package file.')
    for name, limit in ((RELEASE, MAX_METADATA), (RELEASE + '.sig', 65536), (package, MAX_PACKAGE),
                        (package + '.sig', 65536)):
        regular_file(source / name, limit)
        descriptor = os.open(source / name, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(descriptor, 'rb') as data, open(staging / name, 'xb') as copy:
            copied = 0
            for block in iter(lambda: data.read(1 << 20), b''):
                copied += len(block)
                if copied > limit:
                    raise UpdateError(f'{name} grew while it was being copied.')
                copy.write(block)
    return staging


def keep(package, signature, folder):
    """Replace the release kept in `folder` (current or previous) with this package and signature."""
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    for old in folder.iterdir():
        old.unlink()
    for path in (package, signature):
        (folder / path.name).write_bytes(path.read_bytes())


def kept_package(folder):
    packages = sorted(folder.glob(f'{NAME}-*-{ARCH}.pkg.tar.zst')) if folder.is_dir() else []
    return packages[0] if len(packages) == 1 else None


def pacman_install(run, package, state):
    """pacman -U a package this module has already verified. pacman is handed a hard link in a
    folder of its own: with our .sig beside it, pacman checks that signature against its own
    keyring, which lacks the Platinum Reality key, and refuses ("required key missing")."""
    folder = state / 'pacman'
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    target = folder / package.name
    target.unlink(missing_ok=True)
    try:
        os.link(package, target)
    except OSError:
        target.write_bytes(package.read_bytes())
    try:
        return run(['pacman', '-U', '--noconfirm', str(target)], capture_output=True, text=True)
    finally:
        target.unlink(missing_ok=True)


def install(source, report, keyring=KEYRING, state=STATE, run=subprocess.run, now=None,
            current_version=installed_version, missing=missing_dependencies, lock=Path('/var/lib/pacman/db.lck')):
    """Verify the release in `source` and install it with pacman. `report(dict)` receives progress.
    Returns the installed version. Raises UpdateError with a reason the user can act on. Tests
    replace `run`, `current_version`, `missing` and `lock`."""
    if lock.exists():
        raise UpdateError('Another program is installing software. Try again when it has finished.')
    staging = state / 'staging' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%f')
    report({'step': 'copy'})
    try:
        stage(source, staging)
        report({'step': 'verify'})
        release, package = verify_bundle(staging, keyring, current_version(), now)
        needed = missing(package)
        if needed:
            raise UpdateError('This release needs software that is not installed (' + ', '.join(needed) +
                              '). Update the whole system first (pacman -Syu), then try again.')
        # The release installed until now becomes the previous one, for Restore Previous Version.
        current = kept_package(state / 'current')
        if current is not None:
            keep(current, Path(str(current) + '.sig'), state / 'previous')
        snapshot = None
        if run(['sh', '-c', 'command -v snapper'], capture_output=True).returncode == 0:
            made = run(['snapper', 'create', '-t', 'pre', '-p', '-c', 'number',
                        '-d', f'Platinum Software Update {release["version"]}'], capture_output=True, text=True)
            snapshot = made.stdout.strip() if made.returncode == 0 else None
        report({'step': 'install', 'version': release['version']})
        result = pacman_install(run, package, state)
        if snapshot:
            run(['snapper', 'create', '-t', 'post', '--pre-number', snapshot, '-c', 'number',
                 '-d', f'Platinum Software Update {release["version"]}'], capture_output=True)
        if result.returncode != 0:
            last = (result.stderr or result.stdout).strip().splitlines()[-1:] or ['']
            raise UpdateError('pacman could not install the update: ' + last[0])
        keep(package, Path(str(package) + '.sig'), state / 'current')
        return release['version']
    finally:
        if staging.exists():
            for path in staging.iterdir():
                path.unlink()
            staging.rmdir()


def package_version(package):
    """'platinum-reality-0.42-1-x86_64.pkg.tar.zst' -> '0.42-1' (the file name when it is not ours)."""
    match = re.fullmatch(re.escape(NAME) + r'-([^-]+-[^-]+)-[^-]+\.pkg\.tar\.zst', package.name)
    return match.group(1) if match else package.name


def restore_previous(report, keyring=KEYRING, state=STATE, run=subprocess.run):
    """Reinstall the release that was installed before the last update (a deliberate downgrade)."""
    package = kept_package(state / 'previous')
    if package is None:
        raise UpdateError('No previous version was kept. (One is kept from the second update on.)')
    if not gpg_verify(keyring, package, Path(str(package) + '.sig')):
        raise UpdateError('The kept previous version is not signed by the Platinum Reality release key.')
    report({'step': 'install', 'version': package_version(package)})
    result = pacman_install(run, package, state)
    if result.returncode != 0:
        last = (result.stderr or result.stdout).strip().splitlines()[-1:] or ['']
        raise UpdateError('pacman could not restore the previous version: ' + last[0])
    keep(package, Path(str(package) + '.sig'), state / 'current')
    return package_version(package)
