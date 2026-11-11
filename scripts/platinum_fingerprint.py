"""Fingerprint login: which PAM services ask for a finger (docs/fingerprints.md).

Shared by the Fingerprints control panel's root helper (platinum-fingerprint-login) and its tests.
Only lines ending in MARK are ever added or removed, so turning it off gives back each file exactly
as it was, and pacman keeps the edited files across package updates (they are backup files).
"""
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

MARK = '# platinum-fingerprint'
MODULE = 'usr/lib/security/pam_fprintd.so'
# Log In (SDDM) and the Platinum lock and tty1 Log In (swaylock's service): the password field is the
# dialog, so a typed password is checked first; Return with no password (or a wrong one) waits for a
# finger. The lock also verifies the finger itself, while waiting, so there no Return is needed.
LOGIN = ['-auth      [success=1 default=ignore]  pam_unix.so     try_first_pass          ' + MARK,
         '-auth      sufficient                  pam_fprintd.so  max-tries=3 timeout=10  ' + MARK]
# Password boxes (polkit) and sudo: the finger first; the password after 3 tries or 10 seconds,
# and at once when there is no reader or no enrolled finger.
APPROVE = ['-auth      sufficient  pam_fprintd.so  max-tries=3 timeout=10  ' + MARK]
SERVICES = {'sddm': LOGIN, 'swaylock': LOGIN, 'polkit-1': APPROVE, 'sudo': APPROVE}
# polkit ships its service in /usr/lib/pam.d; a marked copy in /etc/pam.d takes its place while on.
VENDOR = {'polkit-1': 'usr/lib/pam.d/polkit-1'}
COPIED = MARK + ': copied from /{} by the Fingerprints control panel'
BACKUPS = 'var/lib/platinum-reality/pam-backups'
# The Platinum Log In for SDDM shows "Or press Return, then touch the fingerprint reader" and lets
# Return through with no password only with this, in SDDM's own per-theme override file.
THEME = 'usr/share/sddm/themes/platinum-reality'
THEME_SETTING = 'fingerprint=true'
KEEP_BACKUPS = 10
MAX_FILE = 64 * 1024


class FingerprintError(Exception):
    """A change that was refused, with the reason shown to the user."""


def available(root=Path('/')):
    """fprintd's PAM module is installed."""
    return (Path(root) / MODULE).is_file()


def _read(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_FILE:
        raise FingerprintError(f'{path.name} is not an ordinary PAM file; nothing was changed.')
    return path.read_text()


def _rules(text):
    return [line for line in text.splitlines() if line.strip() and not line.lstrip().startswith('#')]


def _unmarked(text):
    return ''.join(line for line in text.splitlines(keepends=True) if not line.rstrip('\n').endswith(MARK))


def without(text):
    """The file with every marked line (and our copy's header) taken out."""
    return ''.join(line for line in _unmarked(text).splitlines(keepends=True) if not line.startswith(MARK + ':'))


def with_lines(text, lines):
    """The file with `lines` before its first rule (after the #%PAM-1.0 header and comments)."""
    text = _unmarked(text)
    kept = text.splitlines(keepends=True)
    at = next((i for i, line in enumerate(kept) if line.strip() and not line.lstrip().startswith('#')), len(kept))
    if kept and not kept[-1].endswith('\n') and at == len(kept):
        kept[-1] += '\n'
    return ''.join(kept[:at] + [line + '\n' for line in lines] + kept[at:])


def uses_fingerprint(text):
    return any(line.rstrip().endswith(MARK) for line in text.splitlines())


def status(root=Path('/')):
    """{available, login, sudo}: login is on when any Log In, lock or password-box service has it."""
    root = Path(root)
    on = {}
    for name in SERVICES:
        path = root / 'etc/pam.d' / name
        try:
            on[name] = path.is_file() and not path.is_symlink() and uses_fingerprint(path.read_text())
        except OSError:
            on[name] = False
    return {'available': available(root), 'login': on['sddm'] or on['swaylock'] or on['polkit-1'],
            'sudo': on['sudo']}


def _write(path, text, like=None):
    """Replace `path` atomically, keeping the owner and mode of `like` (the file it replaces)."""
    stat = (like or path).stat() if (like or path).exists() else None
    handle, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', dir=path.parent)
    try:
        with os.fdopen(handle, 'w') as file:
            file.write(text)
            file.flush()
            os.fsync(file.fileno())
        os.chmod(temporary, stat.st_mode & 0o7777 if stat else 0o644)
        if stat and os.geteuid() == 0:
            os.chown(temporary, stat.st_uid, stat.st_gid)
        os.replace(temporary, path)
    except BaseException:
        os.unlink(temporary)
        raise


def _backup(root, names, now):
    folder = Path(root) / BACKUPS / now.strftime('%Y%m%dT%H%M%S%fZ')
    folder.mkdir(parents=True, mode=0o700)
    for name in names:
        source = Path(root) / 'etc/pam.d' / name
        if source.is_file() and not source.is_symlink():
            shutil.copy2(source, folder / name)
    old = sorted(p for p in folder.parent.iterdir() if p.is_dir())
    for stale in old[:-KEEP_BACKUPS]:
        shutil.rmtree(stale)
    return folder


def plan(root, login, sudo):
    """{service: new text, or None to remove our copy} for the services that change."""
    root = Path(root)
    if (login or sudo) and not available(root):
        raise FingerprintError('Fingerprint support (fprintd) is not installed.')
    changes = {}
    for name, lines in SERVICES.items():
        want = sudo if name == 'sudo' else login
        path = root / 'etc/pam.d' / name
        vendor = root / VENDOR[name] if name in VENDOR else None
        if path.exists() or path.is_symlink():
            text = _read(path)
        elif want and vendor and vendor.is_file():
            text = COPIED.format(VENDOR[name]) + '\n' + _read(vendor)
        else:
            continue  # Not installed here (no SDDM, no sudo): nothing to do.
        if not _rules(text):
            raise FingerprintError(f'/etc/pam.d/{name} has no rules; nothing was changed.')
        if want:
            new = with_lines(text, lines)
        elif vendor and text.startswith(COPIED.format(VENDOR[name]) + '\n'):
            new = None  # Our own copy: removing it lets polkit's own file apply again.
        else:
            new = without(text)
        # Only marked lines may differ (the copy header included).
        if new is not None and without(new) != without(text):
            raise FingerprintError(f'Could not change /etc/pam.d/{name} safely; nothing was changed.')
        if new != text or not path.exists():
            changes[name] = new
    return changes


def theme_settings(text, on):
    """theme.conf.user with fingerprint=true in [General] (on) or without it (off)."""
    lines = [line for line in text.splitlines()
             if line.strip() not in (THEME_SETTING, 'fingerprint=false', MARK)]
    if on:
        at = next((i + 1 for i, line in enumerate(lines) if line.strip() == '[General]'), None)
        if at is None:
            lines += ['[General]']
            at = len(lines)
        lines[at:at] = [MARK, THEME_SETTING]
    return '\n'.join(lines) + '\n' if [line for line in lines if line.strip() not in ('', '[General]')] else ''


def _theme(root, on):
    folder = Path(root) / THEME
    if not folder.is_dir():
        return
    path = folder / 'theme.conf.user'
    text = path.read_text() if path.is_file() and not path.is_symlink() else ''
    new = theme_settings(text, on)
    if new == text:
        return
    if new:
        _write(path, new)
    elif path.is_file():
        path.unlink()


def apply(root=Path('/'), login=False, sudo=False, now=None):
    """Turn fingerprint login and sudo on or off. Returns the backup folder, or None if unchanged."""
    root = Path(root)
    changes = plan(root, login, sudo)
    _theme(root, login and available(root))
    if not changes:
        return None
    folder = _backup(root, changes, now or datetime.now(timezone.utc))
    for name, text in changes.items():
        path = root / 'etc/pam.d' / name
        if text is None:
            path.unlink()
        else:
            vendor = root / VENDOR[name] if name in VENDOR else None
            _write(path, text, like=path if path.exists() else vendor)
    return folder


def install_support(report):
    """Install fprintd with pacman (the helper runs this as root)."""
    process = subprocess.run(['pacman', '-S', '--needed', '--noconfirm', 'fprintd'], capture_output=True,
                             text=True, env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'})
    for line in process.stdout.splitlines()[-5:]:
        report({'progress': line})
    if process.returncode != 0:
        detail = (process.stderr.strip().splitlines() or ['pacman failed'])[-1]
        raise FingerprintError(f'Could not install fprintd ({detail}). In Terminal: sudo pacman -Syu fprintd')
