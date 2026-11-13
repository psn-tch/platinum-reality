"""The App Store's package catalog and installs (docs/app-store.md).

Shared by the App Store's helpers and their tests:
- platinum-packages (runs as the user): reads the catalog, refreshes the AUR list, installs an AUR
  helper (yay or paru, the person's choice) and AUR packages with it, and asks
  platinum-packages-admin (through pkexec) for everything else.
- platinum-packages-admin (root, through pkexec): installs, removes and updates official packages
  with pacman, and installs the AUR helper's package built by the user helper.
Everything is read from files already on the computer (pacman's package lists and the AppStream
catalog), so nothing is downloaded until something is installed. The AUR list is downloaded only
when an AUR helper is installed and the person asks for it.
"""
from datetime import datetime, timezone
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

PACMAN_DB = 'var/lib/pacman'
APPSTREAM = ['usr/share/swcatalog', 'usr/share/app-info']
AUR_URL = 'https://aur.archlinux.org/packages-meta-v1.json.gz'
AUR_PAGE = 'https://aur.archlinux.org/packages/{}'
# The AUR helpers the App Store can install and use (Lily Hewitt, 2026-11-12: let people choose paru
# or yay, "just the nice thing to do"), in the order used when the person hasn't chosen.
AUR_HELPERS = {'yay': 'https://aur.archlinux.org/yay-bin.git', 'paru': 'https://aur.archlinux.org/paru-bin.git'}
AUR_MAX = 256 * 1024 * 1024
USER_AGENT = 'PlatinumReality-AppStore/1'
NAME = re.compile(r'^[a-z0-9@_+][a-z0-9@._+-]*$')
LANG = '{http://www.w3.org/XML/1998/namespace}lang'
# The App Store's categories, from the freedesktop main categories (the first one that matches).
CATEGORIES = [('AudioVideo', 'Audio & Video'), ('Audio', 'Audio & Video'), ('Video', 'Audio & Video'),
              ('Development', 'Development'), ('Education', 'Education'), ('Game', 'Games'),
              ('Graphics', 'Graphics'), ('Network', 'Internet'), ('Office', 'Office'), ('Science', 'Science'),
              ('Settings', 'System'), ('System', 'System'), ('Utility', 'Utilities')]
# Removing these would leave the computer unable to start or Platinum Reality unable to run.
PROTECTED = {'base', 'bash', 'coreutils', 'cryptsetup', 'filesystem', 'glibc', 'grub', 'iwd', 'linux',
             'linux-firmware', 'linux-hardened', 'linux-lts', 'linux-zen', 'mkinitcpio', 'pacman', 'pam',
             'platinum-reality', 'polkit', 'python', 'qt6-base', 'sddm', 'sudo', 'sway', 'systemd', 'util-linux'}
STEP = re.compile(r'^\(\s*(\d+)/(\d+)\) (installing|upgrading|reinstalling|downgrading|removing) (\S+)')


class PackageError(Exception):
    """A request that could not be done, with the reason shown to the person."""


# Reading ---------------------------------------------------------------------------------------

def _desc(text):
    """pacman's desc format: %FIELD% then one value per line, then a blank line."""
    fields, key = {}, None
    for line in text.splitlines():
        if line.startswith('%') and line.endswith('%') and len(line) > 2:
            key = line[1:-1]
            fields[key] = []
        elif line and key:
            fields[key].append(line)
        else:
            key = None
    return fields


def _first(fields, key, default=''):
    return fields.get(key, [default])[0]


def _number(fields, key):
    try:
        return int(_first(fields, key, '0'))
    except ValueError:
        return 0


def read_sync(root=Path('/')):
    """{name: package} from pacman's downloaded package lists (/var/lib/pacman/sync/*.db)."""
    packages = {}
    for database in sorted((Path(root) / PACMAN_DB / 'sync').glob('*.db')):
        repo = database.stem
        try:
            with tarfile.open(database) as archive:
                for member in archive:
                    if not member.isfile() or not member.name.endswith('/desc'):
                        continue
                    fields = _desc(archive.extractfile(member).read().decode('utf-8', 'replace'))
                    name = _first(fields, 'NAME')
                    if name and name not in packages:  # The first repository wins, as in pacman.conf.
                        packages[name] = {'name': name, 'version': _first(fields, 'VERSION'),
                                          'description': _first(fields, 'DESC'), 'repo': repo,
                                          'size': _number(fields, 'ISIZE'), 'download': _number(fields, 'CSIZE'),
                                          'url': _first(fields, 'URL')}
        except (OSError, tarfile.TarError, EOFError):
            continue  # A list being rewritten by pacman right now: the rest still show.
    return packages


def read_local(root=Path('/')):
    """{name: installed package} from pacman's local database."""
    installed = {}
    for desc in (Path(root) / PACMAN_DB / 'local').glob('*/desc'):
        try:
            fields = _desc(desc.read_text(errors='replace'))
        except OSError:
            continue
        name = _first(fields, 'NAME')
        if name:
            installed[name] = {'version': _first(fields, 'VERSION'), 'description': _first(fields, 'DESC'),
                               'size': _number(fields, 'SIZE') or _number(fields, 'ISIZE'),
                               'url': _first(fields, 'URL'), 'explicit': _first(fields, 'REASON', '0') != '1'}
    return installed


def _plain(element, tag):
    """The untranslated text of the first `tag` child (AppStream repeats them per language)."""
    for child in element.findall(tag):
        if child.get(LANG) is None:
            return ' '.join((child.text or '').split())
    return ''


def _about(component):
    """AppStream's description (paragraphs and lists) as plain text, untranslated."""
    description = next((d for d in component.findall('description') if d.get(LANG) is None), None)
    if description is None:
        return ''
    parts = []
    for block in description:
        if block.get(LANG) is not None:
            continue
        if block.tag == 'p':
            parts.append(' '.join(''.join(block.itertext()).split()))
        elif block.tag in ('ul', 'ol'):
            parts.append('\n'.join('• ' + ' '.join(''.join(item.itertext()).split())
                                   for item in block if item.get(LANG) is None))
    return '\n\n'.join(part for part in parts if part)


def _category(component):
    names = [c.text for c in component.iter('category') if c.text]
    for key, title in CATEGORIES:
        if key in names:
            return title
    return 'Utilities'


def _icon(component, icons):
    cached = {}
    for icon in component.findall('icon'):
        if icon.get('type') == 'cached' and icon.text:
            cached[int(icon.get('width') or 64)] = icon.text.strip()
    for size in (64, 128, 48):
        if size in cached:
            path = icons / f'{size}x{size}' / cached[size]
            if path.is_file():
                return str(path)
    return ''


def read_appstream(root=Path('/')):
    """Applications (with names, summaries, categories and icons) from the AppStream catalog."""
    apps = []
    for base in APPSTREAM:
        folder = Path(root) / base
        for catalog in sorted((folder / 'xml').glob('*.xml.gz')) + sorted((folder / 'xmls').glob('*.xml.gz')):
            try:
                with gzip.open(catalog) as stream:
                    components = ET.parse(stream).getroot()
            except (OSError, ET.ParseError, EOFError):
                continue
            icons = folder / 'icons' / components.get('origin', catalog.name.split('.')[0])
            for component in components:
                if component.get('type') not in ('desktop-application', 'desktop'):
                    continue
                package = _plain(component, 'pkgname')
                title = _plain(component, 'name')
                if not package or not title:
                    continue
                launch = next((l.text.strip() for l in component.findall('launchable')
                               if l.get('type') == 'desktop-id' and l.text), '')
                apps.append({'id': _plain(component, 'id'), 'title': title, 'package': package,
                             'summary': _plain(component, 'summary'), 'about': _about(component),
                             'category': _category(component), 'icon': _icon(component, icons),
                             'stock': next((i.text.strip() for i in component.findall('icon')
                                            if i.get('type') == 'stock' and i.text), ''),
                             'launch': launch, 'homepage': next((u.text.strip() for u in component.findall('url')
                                                                 if u.get('type') == 'homepage' and u.text), '')})
        if apps:
            break
    return apps


def cache_folder():
    base = os.environ.get('XDG_CACHE_HOME') or str(Path.home() / '.cache')
    return Path(base) / 'platinum-reality/app-store'


def read_aur(folder=None):
    """[package] from the downloaded AUR list, or [] when there is none."""
    path = Path(folder or cache_folder()) / 'aur-packages.json.gz'
    try:
        with gzip.open(path) as stream:
            entries = json.load(stream)
    except (OSError, ValueError, EOFError):
        return []
    packages = []
    for entry in entries if isinstance(entries, list) else []:
        name = entry.get('Name') if isinstance(entry, dict) else None
        if not isinstance(name, str) or not NAME.match(name):
            continue
        packages.append({'name': name, 'version': str(entry.get('Version') or ''),
                         'description': str(entry.get('Description') or ''), 'repo': 'aur',
                         'url': str(entry.get('URL') or ''), 'votes': int(entry.get('NumVotes') or 0),
                         'popularity': float(entry.get('Popularity') or 0),
                         'outOfDate': bool(entry.get('OutOfDate')), 'orphan': entry.get('Maintainer') is None})
    return packages


def aur_updated(folder=None):
    path = Path(folder or cache_folder()) / 'aur-packages.json.gz'
    try:
        return datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    except OSError:
        return ''


def upgradable(pacman='pacman'):
    """{name: newer version} that the downloaded package lists offer (no network: pacman -Qu)."""
    try:
        process = subprocess.run([pacman, '-Qu', '--color', 'never'], capture_output=True, text=True, timeout=60,
                                 env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'})
    except (OSError, subprocess.TimeoutExpired):
        return {}
    updates = {}
    for line in process.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[2] == '->' and '[ignored]' not in line:
            updates[parts[0]] = parts[3]
    return updates


def config_folder():
    base = os.environ.get('XDG_CONFIG_HOME') or str(Path.home() / '.config')
    return Path(base) / 'platinum-reality'


def installed_helpers(root=Path('/'), programs=None):
    """The AUR helpers installed, as programs (whichever package brought them: yay, yay-bin, yay-git)."""
    programs = programs or {}
    return [name for name in AUR_HELPERS if Path(programs.get(name) or Path(root) / 'usr/bin' / name).is_file()]


def chosen_helper(config=None):
    """The helper the person chose, or '' when they haven't."""
    try:
        name = (Path(config or config_folder()) / 'aur-helper').read_text().strip()
    except OSError:
        return ''
    return name if name in AUR_HELPERS else ''


def choose_helper(name, config=None):
    if name not in AUR_HELPERS:
        raise PackageError(f'“{name}” is not an AUR helper the App Store knows.')
    folder = Path(config or config_folder())
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'aur-helper').write_text(name + '\n')


def aur_helper(root=Path('/'), config=None, programs=None):
    """The helper in use: the chosen one if it is installed, else the first one installed; '' for none."""
    installed = installed_helpers(root, programs)
    chosen = chosen_helper(config)
    return chosen if chosen in installed else (installed[0] if installed else '')


def catalog(root=Path('/'), aur_folder=None, pacman='pacman', config=None, programs=None):
    """Everything the App Store lists: packages (official, AUR and installed-only) and applications."""
    sync = read_sync(root)
    local = read_local(root)
    updates = upgradable(pacman) if Path(root) == Path('/') else {}
    packages = dict(sync)
    helper = aur_helper(root, config, programs)
    aur = read_aur(aur_folder) if helper else []
    for entry in aur:
        packages.setdefault(entry['name'], entry)
    for name, mine in local.items():
        if name not in packages:  # Installed from somewhere else (a file, or the AUR before its list).
            packages[name] = {'name': name, 'version': mine['version'], 'description': mine['description'],
                              'repo': 'local', 'size': mine['size'], 'url': mine['url']}
    for name, entry in packages.items():
        mine = local.get(name)
        entry['installed'] = mine['version'] if mine else ''
        entry['explicit'] = bool(mine and mine['explicit'])
        if mine and entry['repo'] == 'aur' and entry['version'] and entry['version'] != mine['version']:
            entry['update'] = entry['version']
        elif name in updates:
            entry['update'] = updates[name]
        if mine and not entry.get('size'):
            entry['size'] = mine['size']
    apps = [app for app in read_appstream(root) if app['package'] in packages]
    return {'packages': sorted(packages.values(), key=lambda p: p['name']), 'apps': apps,
            'aurHelper': helper, 'aurHelpers': installed_helpers(root, programs),
            'aurUpdated': aur_updated(aur_folder) if helper else ''}


# AUR list --------------------------------------------------------------------------------------

def refresh_aur(folder=None, url=AUR_URL, opener=urllib.request.urlopen):
    """Download the AUR's package list (about 10 MB) unless the copy here is still current."""
    folder = Path(folder or cache_folder())
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / 'aur-packages.json.gz'
    request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
    if path.is_file():
        request.add_header('If-Modified-Since', datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
                           .strftime('%a, %d %b %Y %H:%M:%S GMT'))
    handle, temporary = tempfile.mkstemp(prefix='.aur-', dir=folder)
    try:
        try:
            response = opener(request, timeout=60)
        except urllib.error.HTTPError as problem:
            problem.close()
            if problem.code == 304:
                return False
            raise PackageError(f'The AUR list could not be downloaded (HTTP {problem.code}).') from problem
        except (OSError, ValueError) as problem:
            raise PackageError('The AUR list could not be downloaded. Check the network connection.') from problem
        with response, os.fdopen(handle, 'wb') as out:
            handle = None
            total = 0
            while chunk := response.read(1 << 20):
                total += len(chunk)
                if total > AUR_MAX:
                    raise PackageError('The AUR list is unexpectedly large; it was not saved.')
                out.write(chunk)
        with gzip.open(temporary) as stream:  # Only a complete, readable list replaces the old one.
            if not isinstance(json.load(stream), list):
                raise PackageError('The AUR list was not in the expected format; it was not saved.')
        os.replace(temporary, path)
        return True
    except (OSError, ValueError, EOFError) as problem:
        raise PackageError('The AUR list could not be read; the old list was kept.') from problem
    finally:
        if handle is not None:
            os.close(handle)
        Path(temporary).unlink(missing_ok=True)


# Changes (root) --------------------------------------------------------------------------------

def check_names(names):
    if not names:
        raise PackageError('No package was chosen.')
    for name in names:
        if not NAME.match(name):
            raise PackageError(f'“{name}” is not a package name.')
    return list(names)


def explain(output, removing=False):
    """pacman's failure as a sentence for the person."""
    if 'unable to lock database' in output:
        return ('Another program is installing or removing software right now. '
                'Try again when it has finished.')
    if 'failed retrieving file' in output or 'error 404' in output.lower():
        return ('The package lists are out of date, so a file could not be downloaded. '
                'Choose Update All Software…, then try again.')
    match = re.search(r'target not found: (\S+)', output)
    if match and removing:
        return f'“{match.group(1)}” isn’t installed.'
    if match:
        return f'“{match.group(1)}” is not in the package lists. Choose Update All Software…, then try again.'
    match = re.search(r"removing (\S+) breaks dependency '[^']*' required by (\S+)", output)
    if match:
        return f'“{match.group(2)}” needs “{match.group(1)}”, so it can’t be removed.'
    if 'conflicting files' in output or 'conflicts with' in output:
        return 'It conflicts with software that is already installed. Details: ' + _last_error(output)
    if 'Could not resolve host' in output or 'Temporary failure in name resolution' in output:
        return 'The network isn’t connected, so nothing could be downloaded.'
    return _last_error(output) or 'pacman stopped with an error.'


def _last_error(output):
    errors = [line[len('error: '):] for line in output.splitlines() if line.startswith('error: ')]
    return errors[-1] if errors else ''


def run_pacman(arguments, report, pacman='/usr/bin/pacman', removing=False):
    """Run pacman without questions, reporting each line and its install/remove steps."""
    command = [pacman, *arguments, '--noconfirm', '--noprogressbar', '--color', 'never']
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'})
    lines = []
    for line in process.stdout:
        line = line.rstrip()
        if not line:
            continue
        lines.append(line)
        step = STEP.match(line)
        if step:
            report({'step': int(step.group(1)), 'steps': int(step.group(2)), 'doing': step.group(3),
                    'package': step.group(4), 'line': line})
        else:
            report({'progress': line})
    if process.wait() != 0:
        raise PackageError(explain('\n'.join(lines[-60:]), removing))


def install(names, report, pacman='/usr/bin/pacman'):
    run_pacman(['-S', '--needed', *check_names(names)], report, pacman)


def remove(names, report, pacman='/usr/bin/pacman'):
    names = check_names(names)
    kept = sorted(set(names) & PROTECTED)
    if kept:
        raise PackageError(f'“{kept[0]}” is part of the system, so the App Store won’t remove it.')
    run_pacman(['-Rs', *names], report, pacman, removing=True)


def update_all(report, pacman='/usr/bin/pacman'):
    run_pacman(['-Syu'], report, pacman)


def install_file(path, report, pacman='/usr/bin/pacman'):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or not re.search(r'\.pkg\.tar\.(zst|xz)$', path.name):
        raise PackageError('That is not a package file.')
    run_pacman(['-U', str(path)], report, pacman)


# Changes (the user's helper) ---------------------------------------------------------------------

def stream(command, report, env=None, cwd=None):
    """Run a program, reporting its output lines. Returns (exit code, the last lines)."""
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               env=env, cwd=cwd)
    lines = []
    for line in process.stdout:
        line = line.rstrip()
        if line:
            lines.append(line)
            del lines[:-60]
            report({'progress': line})
    return process.wait(), '\n'.join(lines)


def install_helper(name, report, admin, folder=None, git='git', makepkg='makepkg', config=None):
    """Install yay or paru: git and base-devel (official), then its -bin package built here from the
    AUR. It becomes the helper in use."""
    if name not in AUR_HELPERS:
        raise PackageError(f'“{name}” is not an AUR helper the App Store knows.')
    package = name + '-bin'
    report({'progress': 'Installing git and base-devel…'})
    admin(['install', 'git', 'base-devel'])
    folder = Path(folder or cache_folder())
    folder.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=name + '-', dir=folder))
    try:
        report({'progress': f'Downloading {name} from the AUR…'})
        code, output = stream([git, 'clone', '--depth', '1', AUR_HELPERS[name], str(work / package)], report)
        if code != 0:
            raise PackageError(f'{name} could not be downloaded. Check the network connection.')
        report({'progress': f'Building {name}…'})
        code, output = stream([makepkg, '--noconfirm', '--force'], report, cwd=work / package)
        if code != 0:
            raise PackageError(f'{name} could not be built: ' + (output.splitlines() or ['makepkg failed'])[-1])
        built = sorted(p for p in (work / package).glob(package + '-*.pkg.tar.*') if '-debug-' not in p.name)
        if len(built) != 1:
            raise PackageError(f'{name} was built, but its package could not be found.')
        report({'progress': f'Installing {name}…'})
        admin(['install-file', str(built[0])])
    finally:
        shutil.rmtree(work, ignore_errors=True)
    choose_helper(name, config)


def helper_command(names, helper='yay', program=None):
    """The helper, answering its own questions; it asks for the administrator's password with pkexec."""
    # aur/<name>: the AUR package itself, never an official one that provides the same name (yay's
    # --aur alone still picks those, as pfetch-rs for pfetch showed).
    targets = ['aur/' + name for name in check_names(names)]
    if helper == 'paru':
        return [program or 'paru', '-S', '--needed', '--noconfirm', '--skipreview', '--removemake',
                '--sudo', 'pkexec', '--color', 'never', *targets]
    if helper == 'yay':
        return [program or 'yay', '-S', '--needed', '--noconfirm', '--answerclean', 'None', '--answerdiff', 'None',
                '--answeredit', 'None', '--removemake', '--sudo', 'pkexec', '--color', 'never', *targets]
    raise PackageError('No AUR helper is installed. Install yay or paru first.')


def install_aur(names, report, helper='yay', program=None):
    code, output = stream(helper_command(names, helper, program), report)
    if code != 0:
        if 'One or more PGP signatures could not be verified' in output:
            raise PackageError('Its source code is signed with a key this computer doesn’t know. '
                               f'In Terminal: {helper} -S ' + ' '.join(names))
        raise PackageError(explain(output) if 'error: ' in output else
                           f'{helper} stopped with an error: ' + (output.splitlines() or [''])[-1])
