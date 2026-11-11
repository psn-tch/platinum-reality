#!/usr/bin/env python3
"""The App Store's catalog and changes, on a throwaway root with a stand-in pacman, git and makepkg."""
import gzip
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import urllib.error

HERE = Path(__file__).resolve().parent
SCRIPTS = HERE.parent / 'scripts'
spec = importlib.util.spec_from_file_location('platinum_packages', SCRIPTS / 'platinum_packages.py')
packages = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packages)

APPSTREAM = '''<?xml version="1.0" encoding="UTF-8"?>
<components version="1.0" origin="archlinux-arch-extra">
  <component type="desktop-application">
    <id>org.example.Paint</id>
    <name>Paint</name>
    <name xml:lang="fr">Peinture</name>
    <summary>Draw pictures</summary>
    <summary xml:lang="fr">Dessiner</summary>
    <description>
      <p>Paint draws   pictures.</p>
      <p xml:lang="fr">Dessine.</p>
      <ul><li>Brushes</li><li>Layers</li></ul>
    </description>
    <pkgname>paint</pkgname>
    <launchable type="desktop-id">org.example.Paint.desktop</launchable>
    <url type="homepage">https://paint.example</url>
    <icon type="cached" width="64" height="64">paint_paint.png</icon>
    <categories><category>Graphics</category><category>Utility</category></categories>
  </component>
  <component type="desktop-application">
    <id>org.example.Missing</id><name>Missing</name><pkgname>not-in-any-list</pkgname>
  </component>
  <component type="addon"><id>x</id><name>Addon</name><pkgname>paint</pkgname></component>
  <component type="desktop-application">
    <id>org.example.Chess</id><name>Chess</name><pkgname>chess</pkgname>
    <icon type="stock">gnome-chess</icon>
    <categories><category>Game</category><category>BoardGame</category></categories>
  </component>
</components>
'''


def desc(fields):
    return ''.join(f'%{key}%\n' + '\n'.join(values if isinstance(values, list) else [values]) + '\n\n'
                   for key, values in fields.items()).encode()


def write_db(path, entries):
    with tarfile.open(path, 'w:gz') as archive:
        for fields in entries:
            data = desc(fields)
            info = tarfile.TarInfo(f"{fields['NAME']}-{fields['VERSION']}/desc")
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


FAKE_PACMAN = '''#!/bin/sh
# Records its arguments; prints what the test asks for and exits with its code. pacman gets a clean
# environment, so the requests are files next to this script.
here=$(dirname "$0")
printf '%s\\n' "$*" >> "$here/log"
[ -f "$here/output" ] && printf '%b' "$(cat "$here/output")"
exit "$(cat "$here/exit" 2>/dev/null || echo 0)"
'''


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        sync = self.root / 'var/lib/pacman/sync'
        sync.mkdir(parents=True)
        write_db(sync / 'core.db', [{'NAME': 'bash', 'VERSION': '5.3-1', 'DESC': 'The shell', 'ISIZE': '9000',
                                     'CSIZE': '2000', 'URL': 'https://gnu.org/bash'}])
        write_db(sync / 'extra.db', [{'NAME': 'paint', 'VERSION': '2.0-1', 'DESC': 'A paint program', 'ISIZE': '500',
                                      'CSIZE': '100'},
                                     {'NAME': 'chess', 'VERSION': '1.0-1', 'DESC': 'Chess'},
                                     {'NAME': 'bash', 'VERSION': '9.9-1', 'DESC': 'Shadowed by core'}])
        (sync / 'broken.db').write_bytes(b'not a database')
        local = self.root / 'var/lib/pacman/local'
        for fields in ({'NAME': 'bash', 'VERSION': '5.2-1', 'DESC': 'The shell', 'SIZE': '8000'},
                       {'NAME': 'libdep', 'VERSION': '1-1', 'DESC': 'A library', 'SIZE': '10', 'REASON': '1'},
                       {'NAME': 'handmade', 'VERSION': '0.1-1', 'DESC': 'Built here', 'SIZE': '77'}):
            folder = local / f"{fields['NAME']}-{fields['VERSION']}"
            folder.mkdir(parents=True)
            (folder / 'desc').write_bytes(desc(fields))
        catalog = self.root / 'usr/share/swcatalog'
        (catalog / 'xml').mkdir(parents=True)
        with gzip.open(catalog / 'xml/extra.xml.gz', 'wt') as stream:
            stream.write(APPSTREAM)
        icons = catalog / 'icons/archlinux-arch-extra/64x64'
        icons.mkdir(parents=True)
        (icons / 'paint_paint.png').write_bytes(b'png')
        self.cache = self.root / 'cache'

    def tearDown(self):
        self.folder.cleanup()

    def write_aur(self, entries):
        self.cache.mkdir(exist_ok=True)
        with gzip.open(self.cache / 'aur-packages.json.gz', 'wt') as stream:
            json.dump(entries, stream)

    def catalog(self, yay=False):
        marker = self.root / 'yay'
        if yay:
            marker.write_text('')
        return packages.catalog(self.root, self.cache, yay=str(marker))

    def test_official_packages_installed_versions_and_installed_only_packages(self):
        result = self.catalog()
        by_name = {p['name']: p for p in result['packages']}
        self.assertEqual(sorted(by_name), ['bash', 'chess', 'handmade', 'libdep', 'paint'])
        self.assertEqual(by_name['bash']['repo'], 'core')  # The first repository wins.
        self.assertEqual(by_name['bash']['version'], '5.3-1')
        self.assertEqual(by_name['bash']['installed'], '5.2-1')
        self.assertTrue(by_name['bash']['explicit'])
        self.assertEqual(by_name['paint']['installed'], '')
        self.assertEqual(by_name['paint']['size'], 500)
        self.assertEqual(by_name['libdep']['repo'], 'local')
        self.assertFalse(by_name['libdep']['explicit'])
        self.assertEqual(by_name['handmade']['size'], 77)
        self.assertFalse(result['yay'])

    def test_applications_come_from_appstream_untranslated_with_category_and_icon(self):
        apps = {a['id']: a for a in self.catalog()['apps']}
        self.assertEqual(sorted(apps), ['org.example.Chess', 'org.example.Paint'])  # Not the addon or Missing.
        paint = apps['org.example.Paint']
        self.assertEqual((paint['title'], paint['summary'], paint['package']), ('Paint', 'Draw pictures', 'paint'))
        self.assertEqual(paint['about'], 'Paint draws pictures.\n\n• Brushes\n• Layers')
        self.assertEqual(paint['category'], 'Graphics')
        self.assertEqual(paint['launch'], 'org.example.Paint.desktop')
        self.assertEqual(paint['homepage'], 'https://paint.example')
        self.assertTrue(paint['icon'].endswith('64x64/paint_paint.png'))
        self.assertEqual(apps['org.example.Chess']['category'], 'Games')
        self.assertEqual(apps['org.example.Chess']['icon'], '')
        self.assertEqual(apps['org.example.Chess']['stock'], 'gnome-chess')
        self.assertEqual(paint['stock'], '')

    def test_aur_is_listed_only_with_yay_and_never_replaces_an_official_package(self):
        self.write_aur([{'Name': 'aurthing', 'Version': '3-1', 'Description': 'From the AUR', 'NumVotes': 12,
                         'Popularity': 1.5, 'OutOfDate': None, 'Maintainer': 'someone'},
                        {'Name': 'paint', 'Version': '99-1', 'Description': 'An AUR copy'},
                        {'Name': 'handmade', 'Version': '0.2-1', 'Maintainer': None},
                        {'Name': '--evil', 'Version': '1'}, 'nonsense'])
        self.assertNotIn('aurthing', [p['name'] for p in self.catalog()['packages']])
        result = self.catalog(yay=True)
        by_name = {p['name']: p for p in result['packages']}
        self.assertTrue(result['yay'])
        self.assertTrue(result['aurUpdated'])
        self.assertEqual(by_name['aurthing']['repo'], 'aur')
        self.assertEqual(by_name['aurthing']['votes'], 12)
        self.assertEqual(by_name['paint']['repo'], 'extra')
        self.assertEqual(by_name['handmade']['repo'], 'aur')  # Installed from the AUR: now it has an update.
        self.assertEqual(by_name['handmade']['update'], '0.2-1')
        self.assertTrue(by_name['handmade']['orphan'])
        self.assertNotIn('--evil', by_name)

    def test_yay_is_the_program_whichever_package_installed_it(self):
        self.write_aur([{'Name': 'aurthing', 'Version': '3-1'}])
        self.assertFalse(packages.catalog(self.root, self.cache)['yay'])
        (self.root / 'usr/bin').mkdir(parents=True)
        (self.root / 'usr/bin/yay').write_text('')
        result = packages.catalog(self.root, self.cache)
        self.assertTrue(result['yay'])
        self.assertIn('aurthing', [p['name'] for p in result['packages']])

    def test_a_damaged_aur_list_lists_nothing_from_the_aur(self):
        self.cache.mkdir()
        (self.cache / 'aur-packages.json.gz').write_bytes(b'garbage')
        self.assertEqual([p for p in self.catalog(yay=True)['packages'] if p['repo'] == 'aur'], [])


class RefreshTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.cache = Path(self.folder.name)

    def tearDown(self):
        self.folder.cleanup()

    def test_a_complete_list_replaces_the_old_one_and_a_broken_one_does_not(self):
        good = gzip.compress(json.dumps([{'Name': 'x', 'Version': '1'}]).encode())
        self.assertTrue(packages.refresh_aur(self.cache, opener=lambda request, timeout: io.BytesIO(good)))
        self.assertEqual([p['name'] for p in packages.read_aur(self.cache)], ['x'])
        with self.assertRaises(packages.PackageError):
            packages.refresh_aur(self.cache, opener=lambda request, timeout: io.BytesIO(b'truncated'))
        with self.assertRaises(packages.PackageError):
            packages.refresh_aur(self.cache, opener=lambda r, timeout: io.BytesIO(gzip.compress(b'{"a": 1}')))
        self.assertEqual([p['name'] for p in packages.read_aur(self.cache)], ['x'])
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()), ['aur-packages.json.gz'])

    def test_not_modified_keeps_the_list_and_asks_with_its_date(self):
        (self.cache / 'aur-packages.json.gz').write_bytes(gzip.compress(b'[]'))
        seen = []

        def opener(request, timeout):
            seen.append(request.get_header('If-modified-since'))
            raise urllib.error.HTTPError(request.full_url, 304, 'Not Modified', {}, None)
        self.assertFalse(packages.refresh_aur(self.cache, opener=opener))
        self.assertTrue(seen[0].endswith('GMT'))

    def test_network_failure_explains_itself(self):
        def opener(request, timeout):
            raise urllib.error.URLError('no route')
        with self.assertRaisesRegex(packages.PackageError, 'network'):
            packages.refresh_aur(self.cache, opener=opener)


class ChangeTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.pacman = self.root / 'pacman'
        self.pacman.write_text(FAKE_PACMAN)
        self.pacman.chmod(0o755)
        self.log = self.root / 'log'
        self.reports = []

    def tearDown(self):
        self.folder.cleanup()

    def answer(self, output, code=0):
        (self.root / 'output').write_text(output)
        (self.root / 'exit').write_text(str(code))

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_install_reports_steps_and_asks_pacman_no_questions(self):
        self.answer('resolving dependencies...\\n( 1/2) installing libdep\\n( 2/2) installing paint\\n')
        packages.install(['paint'], self.reports.append, str(self.pacman))
        self.assertEqual(self.calls(), ['-S --needed paint --noconfirm --noprogressbar --color never'])
        self.assertIn({'progress': 'resolving dependencies...'}, self.reports)
        self.assertIn({'step': 2, 'steps': 2, 'doing': 'installing', 'package': 'paint'}, self.reports)

    def test_names_that_could_be_options_are_refused_before_pacman_runs(self):
        for names in (['--root=/tmp'], ['-Rns'], ['Upper'], [], ['ok', 'b c']):
            with self.assertRaises(packages.PackageError):
                packages.install(names, self.reports.append, str(self.pacman))
        self.assertEqual(self.calls(), [])

    def test_the_system_is_never_removed(self):
        with self.assertRaisesRegex(packages.PackageError, 'part of the system'):
            packages.remove(['paint', 'platinum-reality'], self.reports.append, str(self.pacman))
        self.assertEqual(self.calls(), [])
        packages.remove(['paint'], self.reports.append, str(self.pacman))
        self.assertEqual(self.calls(), ['-Rs paint --noconfirm --noprogressbar --color never'])

    def test_pacman_failures_become_sentences(self):
        cases = {'error: failed to init transaction (unable to lock database)\\n': 'Another program',
                 "error: failed retrieving file 'x.pkg.tar.zst' from mirror : The requested URL returned error: 404\\n":
                     'out of date',
                 'error: target not found: nothere\\n': '“nothere” is not in the package lists',
                 ":: removing libdep breaks dependency 'libdep' required by paint\\n"
                 'error: failed to prepare transaction (could not satisfy dependencies)\\n':
                     '“paint” needs “libdep”',
                 'error: something new\\n': 'something new'}
        for output, expected in cases.items():
            self.answer(output, 1)
            with self.assertRaises(packages.PackageError) as caught:
                packages.install(['paint'], self.reports.append, str(self.pacman))
            self.assertIn(expected, str(caught.exception))

    def test_removing_something_that_is_not_installed_says_so(self):
        self.answer('error: target not found: pfetch\\n', 1)
        with self.assertRaisesRegex(packages.PackageError, '“pfetch” isn’t installed'):
            packages.remove(['pfetch'], self.reports.append, str(self.pacman))

    def test_update_all_is_a_full_upgrade_and_install_file_only_takes_package_files(self):
        packages.update_all(self.reports.append, str(self.pacman))
        self.assertEqual(self.calls()[-1], '-Syu --noconfirm --noprogressbar --color never')
        package = self.root / 'yay-bin-13-1-x86_64.pkg.tar.zst'
        package.write_bytes(b'')
        packages.install_file(package, self.reports.append, str(self.pacman))
        self.assertEqual(self.calls()[-1], f'-U {package} --noconfirm --noprogressbar --color never')
        (self.root / 'link.pkg.tar.zst').symlink_to(package)
        for bad in (self.root / 'notes.txt', self.root / 'link.pkg.tar.zst', self.root / 'missing.pkg.tar.zst'):
            with self.assertRaises(packages.PackageError):
                packages.install_file(bad, self.reports.append, str(self.pacman))
        self.assertEqual(len(self.calls()), 2)

    def test_install_yay_builds_yay_bin_and_hands_only_its_package_to_the_administrator(self):
        bin_folder = self.root / 'bin'
        bin_folder.mkdir()
        (bin_folder / 'git').write_text('#!/bin/sh\nmkdir -p "$5"\necho cloned "$4"\n')
        (bin_folder / 'makepkg').write_text('#!/bin/sh\ntouch yay-bin-13.0.1-1-x86_64.pkg.tar.zst '
                                            'yay-bin-debug-13.0.1-1-x86_64.pkg.tar.zst\necho built\n')
        for tool in bin_folder.iterdir():
            tool.chmod(0o755)
        asked, held = [], []

        def admin(arguments):
            asked.append(arguments)
            if arguments[0] == 'install-file':
                held.append(Path(arguments[1]).exists())
        packages.install_yay(self.reports.append, admin, self.root / 'cache', str(bin_folder / 'git'),
                             str(bin_folder / 'makepkg'))
        self.assertEqual(asked[0], ['install', 'git', 'base-devel'])
        self.assertEqual(asked[1][0], 'install-file')
        self.assertTrue(asked[1][1].endswith('/yay-bin/yay-bin-13.0.1-1-x86_64.pkg.tar.zst'))
        self.assertEqual(held, [True])
        self.assertIn({'progress': f'cloned {packages.YAY_GIT}'}, self.reports)
        self.assertEqual(list((self.root / 'cache').iterdir()), [])  # The build folder is gone.

    def test_yay_answers_its_own_questions_and_uses_pkexec(self):
        command = packages.yay_command(['aurthing'])
        for flag in ('--noconfirm', '--needed', '--removemake'):
            self.assertIn(flag, command)
        self.assertEqual(command[command.index('--sudo') + 1], 'pkexec')
        for menu in ('--answerclean', '--answerdiff', '--answeredit'):
            self.assertEqual(command[command.index(menu) + 1], 'None')
        self.assertEqual(command[-1], 'aur/aurthing')  # Never an official package providing it.
        with self.assertRaises(packages.PackageError):
            packages.yay_command(['--sudo=sh'])


class HelperTests(unittest.TestCase):
    """The two programs, end to end: the user's helper runs the administrator's directly (no pkexec)."""
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.env = dict(os.environ, PLATINUM_PACKAGES_ELEVATE='',
                        PLATINUM_PACKAGES_ADMIN=str(SCRIPTS / 'platinum-packages-admin'))

    def tearDown(self):
        self.folder.cleanup()

    def run_helper(self, *arguments):
        process = subprocess.run([sys.executable, '-I', str(SCRIPTS / 'platinum-packages'), *arguments],
                                 capture_output=True, text=True, env=self.env, timeout=60)
        return process.returncode, [json.loads(line) for line in process.stdout.splitlines() if line.strip()]

    def test_usage_and_bad_names(self):
        self.assertEqual(self.run_helper('frobnicate')[0], 2)
        code, lines = self.run_helper('install', '--evil')
        self.assertEqual(code, 1)
        self.assertIn('is not a package name', lines[-1]['error'])

    def test_the_administrator_helper_refuses_without_root_and_its_error_comes_through(self):
        if os.geteuid() == 0:
            self.skipTest('runs as root')
        code, lines = self.run_helper('install', 'paint')
        self.assertEqual(code, 1)
        self.assertEqual(lines[-1], {'error': 'Installing software needs an administrator.'})

    def test_the_administrator_helper_only_accepts_its_commands(self):
        process = subprocess.run([sys.executable, '-I', str(SCRIPTS / 'platinum-packages-admin'), 'shell', 'x'],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(process.returncode, 2)

    def test_catalog_is_one_json_document(self):
        code, lines = self.run_helper('catalog')
        self.assertEqual(code, 0)
        self.assertEqual(len(lines), 1)
        self.assertEqual(sorted(lines[0]), ['apps', 'aurUpdated', 'packages', 'yay'])


if __name__ == '__main__':
    unittest.main()
