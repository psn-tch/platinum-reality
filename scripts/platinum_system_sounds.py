"""Mac sounds only: removing the Arch and KDE desktop sounds, or getting them back (docs/alert-sounds.md).

Lily Hewitt, 2026-11-12: "i want them GONE", then: an option in the Sound control panel, for everyone,
to remove them or download them again. Shared by the root helper (platinum-system-sounds) and its tests.

The packages stay installed, so nothing that depends on them breaks (libcanberra needs
sound-theme-freedesktop, plasma-workspace needs ocean-sound-theme). Their sound files are deleted, and
a NoExtract line in pacman.conf keeps updates from putting them back. Getting them back takes that line
out and reinstalls the packages that own them. Only the two lines this file writes are ever removed
from pacman.conf (the marker comment, and the NoExtract line right after it).
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

MARK = '# Platinum Reality: no Arch/KDE desktop sounds'
SOUNDS = 'usr/share/sounds'
# The sound themes' folders; Oxygen-* are loose files of the oxygen theme.
FOLDERS = ['freedesktop', 'ocean', 'oxygen', 'plasma-bigscreen']
LOOSE = 'Oxygen-'
PATTERNS = [f'{SOUNDS}/{name}/*' for name in FOLDERS] + [f'{SOUNDS}/{LOOSE}*']
NO_EXTRACT = 'NoExtract   = ' + ' '.join(PATTERNS)
CONF = 'etc/pacman.conf'
LOCAL_DB = 'var/lib/pacman/local'
ENVIRONMENT = {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'}


class SoundsError(Exception):
    """A change that failed, with the reason shown to the user."""


def _marked(lines):
    """Index of our marker line, or -1. Lily's first script wrote the same marker with her name and date."""
    return next((i for i, line in enumerate(lines) if line.startswith(MARK)), -1)


def removed(root=Path('/')):
    """The desktop sounds are kept out: our NoExtract line is in pacman.conf."""
    try:
        return _marked((Path(root) / CONF).read_text().splitlines()) >= 0
    except OSError:
        return False


def present(root=Path('/')):
    """The desktop sound folders and loose files still on disk."""
    folder = Path(root) / SOUNDS
    if not folder.is_dir():
        return []
    return sorted(p.name for p in folder.iterdir() if p.name in FOLDERS or p.name.startswith(LOOSE))


def owners(root=Path('/')):
    """Installed packages whose files include desktop sounds, from pacman's local database (which
    still lists files that were deleted)."""
    found = []
    database = Path(root) / LOCAL_DB
    if not database.is_dir():
        return found
    prefixes = tuple(f'{SOUNDS}/{name}/' for name in FOLDERS) + (f'{SOUNDS}/{LOOSE}',)
    for entry in sorted(database.iterdir()):
        try:
            files = (entry / 'files').read_text(errors='replace').splitlines()
            name = (entry / 'desc').read_text(errors='replace').split('%NAME%\n', 1)[1].split('\n', 1)[0]
        except (OSError, IndexError):
            continue
        if any(line.startswith(prefixes) for line in files):
            found.append(name)
    return found


def status(root=Path('/')):
    """{removed, present, packages}"""
    return {'removed': removed(root), 'present': present(root), 'packages': owners(root)}


def with_no_extract(text):
    """pacman.conf with our marker and NoExtract line in [options], after its #NoExtract example."""
    lines = text.splitlines()
    if _marked(lines) >= 0:
        return text
    start = next((i for i, line in enumerate(lines) if line.strip() == '[options]'), None)
    if start is None:
        raise SoundsError('pacman.conf has no [options] section; nothing was changed.')
    end = next((i for i in range(start + 1, len(lines)) if lines[i].lstrip().startswith('[')), len(lines))
    at = next((i + 1 for i in range(start + 1, end) if lines[i].replace(' ', '').startswith('#NoExtract=')), None)
    if at is None:  # No example line: at the end of [options], before its trailing blank lines.
        at = end
        while at > start + 1 and not lines[at - 1].strip():
            at -= 1
    lines[at:at] = [MARK + ' (the Sound control panel)', NO_EXTRACT]
    return '\n'.join(lines) + '\n'


def without_no_extract(text):
    """pacman.conf without our two lines (and nothing else)."""
    lines = text.splitlines()
    at = _marked(lines)
    if at < 0:
        return text
    key, _, value = (lines[at + 1] if at + 1 < len(lines) else '').partition('=')
    ours = key.strip() == 'NoExtract' and set(value.split()) <= set(PATTERNS)
    del lines[at:at + (2 if ours else 1)]
    return '\n'.join(lines) + '\n'


def _check(path, run, want):
    """pacman itself reads the new file, and its NoExtract has (or lacks) our patterns."""
    process = run(['pacman-conf', '--config', str(path), 'NoExtract'], capture_output=True, text=True, env=ENVIRONMENT)
    if process.returncode != 0:
        return False
    listed = set(process.stdout.split())
    return set(PATTERNS) <= listed if want else not (set(PATTERNS) & listed)


def _rewrite(root, change, want, run):
    path = Path(root) / CONF
    if path.is_symlink() or not path.is_file():
        raise SoundsError('pacman.conf is not an ordinary file; nothing was changed.')
    text = path.read_text()
    new = change(text)
    if new == text:
        return
    stat = path.stat()
    handle, temporary = tempfile.mkstemp(prefix='.pacman.conf.', dir=path.parent)
    try:
        with os.fdopen(handle, 'w') as file:
            file.write(new)
            file.flush()
            os.fsync(file.fileno())
        os.chmod(temporary, stat.st_mode & 0o7777)
        if os.geteuid() == 0:
            os.chown(temporary, stat.st_uid, stat.st_gid)
        if not _check(temporary, run, want):
            raise SoundsError('pacman could not read the changed pacman.conf, so nothing was changed.')
        backup = path.with_name('pacman.conf.before-sounds')
        if not backup.exists():
            shutil.copy2(path, backup)
        os.replace(temporary, path)
    except BaseException:
        if os.path.exists(temporary):
            os.unlink(temporary)
        raise


def remove(root=Path('/'), run=subprocess.run):
    """Keep the desktop sounds out of updates, then delete them."""
    _rewrite(root, with_no_extract, True, run)
    folder = Path(root) / SOUNDS
    for name in present(root):
        path = folder / name
        if path.is_symlink() or not path.is_dir():
            path.unlink()
        else:
            shutil.rmtree(path)


def restore(root=Path('/'), run=subprocess.run):
    """Let updates bring the desktop sounds again, and download them now."""
    _rewrite(root, without_no_extract, False, run)
    packages = owners(root)
    if not packages:
        return
    process = run(['pacman', '-S', '--noconfirm', '--noprogressbar', '--color', 'never', *packages],
                  capture_output=True, text=True, env=ENVIRONMENT)
    if process.returncode != 0:
        detail = ((process.stderr or process.stdout).strip().splitlines() or ['pacman failed'])[-1]
        raise SoundsError(f'The sounds could not be downloaded ({detail}). They come back by themselves the '
                          'next time Update All Software… updates them.')
