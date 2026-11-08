#!/usr/bin/env python3
"""Platinum Software Update's release verification, with throwaway signing keys."""
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'platinum_update', Path(__file__).resolve().parents[1] / 'scripts/platinum_update.py')
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)

NOW = datetime(2026, 11, 9, tzinfo=timezone.utc)


class Keys:
    """A temporary GnuPG home with one signing key, and its public keyring for gpgv."""
    def __init__(self, folder, name):
        self.home = Path(folder) / ('gnupg-' + name)
        self.home.mkdir(mode=0o700)
        self.run('--quick-gen-key', f'{name} <{name}@example.invalid>', 'ed25519', 'sign', 'never')
        self.keyring = Path(folder) / (name + '.gpg')
        self.keyring.write_bytes(self.run('--export'))

    def run(self, *arguments):
        return subprocess.run(['gpg', '--homedir', str(self.home), '--batch', '--pinentry-mode', 'loopback',
                               '--passphrase', '', *arguments], capture_output=True, check=True).stdout

    def sign(self, path):
        signature = Path(str(path) + '.sig')
        signature.unlink(missing_ok=True)
        self.run('--detach-sign', '-o', str(signature), str(path))


def bundle(folder, keys, version='0.41-1', valid=timedelta(days=180), payload=b'package bytes', **changes):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    package = folder / f'platinum-reality-{version}-x86_64.pkg.tar.zst'
    package.write_bytes(payload)
    keys.sign(package)
    release = {'format': 1, 'name': 'platinum-reality', 'version': version, 'date': '2026-11-08T12:00:00Z',
               'valid_until': (NOW + valid).isoformat(), 'arch': 'x86_64', 'package': package.name,
               'sha256': hashlib.sha256(payload).hexdigest(), 'size': len(payload), 'notes': 'Notes.'}
    release.update(changes)
    (folder / 'release.json').write_text(json.dumps(release))
    keys.sign(folder / 'release.json')
    return folder


class VerificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.keys = Keys(cls.temporary.name, 'release')
        cls.other = Keys(cls.temporary.name, 'stranger')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def folder(self, name):
        return Path(self.temporary.name) / self.id().rsplit('.', 1)[1] / name

    def test_a_signed_newer_release_is_accepted(self):
        folder = bundle(self.folder('good'), self.keys)
        release, package = update.verify_bundle(folder, self.keys.keyring, installed='0.40-1', now=NOW)
        self.assertEqual(release['version'], '0.41-1')
        self.assertEqual(package.name, 'platinum-reality-0.41-1-x86_64.pkg.tar.zst')

    def test_older_or_same_versions_are_refused(self):
        folder = bundle(self.folder('old'), self.keys, version='0.39-1')
        with self.assertRaisesRegex(update.UpdateError, 'not newer'):
            update.verify_bundle(folder, self.keys.keyring, installed='0.40-1', now=NOW)
        folder = bundle(self.folder('same'), self.keys, version='0.40-1')
        with self.assertRaisesRegex(update.UpdateError, 'not newer'):
            update.verify_bundle(folder, self.keys.keyring, installed='0.40-1', now=NOW)
        update.verify_bundle(folder, self.keys.keyring, installed='0.40-1', now=NOW, allow_same=True)
        # pacman's ordering, not text: 0.100 is newer than 0.99; 1.0 newer than 0.99.
        update.verify_bundle(bundle(self.folder('hundred'), self.keys, version='0.100-1'),
                             self.keys.keyring, installed='0.99-1', now=NOW)

    def test_a_changed_package_is_refused_even_with_a_matching_checksum(self):
        folder = bundle(self.folder('swapped'), self.keys)
        package = folder / 'platinum-reality-0.41-1-x86_64.pkg.tar.zst'
        package.write_bytes(b'package bytez')
        with self.assertRaisesRegex(update.UpdateError, 'checksum'):
            update.verify_bundle(folder, self.keys.keyring, installed=None, now=NOW)
        # An attacker who also rewrites release.json cannot sign it with the release key.
        release = json.loads((folder / 'release.json').read_text())
        release['sha256'] = hashlib.sha256(b'package bytez').hexdigest()
        (folder / 'release.json').write_text(json.dumps(release))
        with self.assertRaisesRegex(update.UpdateError, 'not signed'):
            update.verify_bundle(folder, self.keys.keyring, installed=None, now=NOW)
        # Signed by someone else: refused too.
        self.other.sign(folder / 'release.json')
        with self.assertRaisesRegex(update.UpdateError, 'not signed'):
            update.verify_bundle(folder, self.keys.keyring, installed=None, now=NOW)

    def test_the_package_signature_is_checked_too(self):
        folder = bundle(self.folder('package-signature'), self.keys)
        self.other.sign(folder / 'platinum-reality-0.41-1-x86_64.pkg.tar.zst')
        with self.assertRaisesRegex(update.UpdateError, 'package is not signed'):
            update.verify_bundle(folder, self.keys.keyring, installed=None, now=NOW)

    def test_expired_descriptions_and_other_packages_are_refused(self):
        with self.assertRaisesRegex(update.UpdateError, 'expired'):
            update.verify_bundle(bundle(self.folder('expired'), self.keys, valid=timedelta(days=-1)),
                                 self.keys.keyring, installed=None, now=NOW)
        with self.assertRaisesRegex(update.UpdateError, 'not a Platinum Reality release'):
            update.verify_bundle(bundle(self.folder('name'), self.keys, name='something-else'),
                                 self.keys.keyring, installed=None, now=NOW)
        with self.assertRaisesRegex(update.UpdateError, 'aarch64'):
            update.verify_bundle(bundle(self.folder('arch'), self.keys, arch='aarch64'),
                                 self.keys.keyring, installed=None, now=NOW)
        with self.assertRaisesRegex(update.UpdateError, 'unexpected package'):
            update.verify_bundle(bundle(self.folder('file'), self.keys, package='../../etc/passwd'),
                                 self.keys.keyring, installed=None, now=NOW)

    def test_links_and_missing_files_are_refused(self):
        folder = bundle(self.folder('link'), self.keys)
        package = folder / 'platinum-reality-0.41-1-x86_64.pkg.tar.zst'
        real = folder / 'real.bin'
        package.rename(real)
        os.symlink(real, package)
        with self.assertRaisesRegex(update.UpdateError, 'not a plain file'):
            update.verify_bundle(folder, self.keys.keyring, installed=None, now=NOW)
        (folder / 'release.json.sig').unlink()
        with self.assertRaisesRegex(update.UpdateError, 'missing release.json.sig'):
            update.verify_bundle(folder, self.keys.keyring, installed=None, now=NOW)

    def test_without_the_keyring_nothing_verifies(self):
        folder = bundle(self.folder('nokeys'), self.keys)
        with self.assertRaisesRegex(update.UpdateError, 'not signed'):
            update.verify_bundle(folder, Path(self.temporary.name) / 'absent.gpg', installed=None, now=NOW)


class Pacman:
    """Stands in for pacman and snapper: records commands, answers like them."""
    def __init__(self, fail=False, snapper=False):
        self.calls, self.fail, self.snapper, self.seen = [], fail, snapper, []

    def __call__(self, command, **_):
        self.calls.append(command)
        class Result:
            pass
        result = Result()
        result.stdout, result.stderr, result.returncode = '', '', 0
        if command[:2] == ['sh', '-c']:
            result.returncode = 0 if self.snapper else 1
        elif command[0] == 'snapper':
            result.stdout = '42\n'
        elif command[:2] == ['pacman', '-U']:
            # What pacman would see: the package, and whether a .sig sits beside it.
            self.seen.append((Path(command[3]).is_file(), Path(command[3] + '.sig').exists()))
            if self.fail:
                result.returncode, result.stderr = 1, 'error: failed to commit transaction (conflicting files)\n'
        return result


class InstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.keys = Keys(cls.temporary.name, 'release')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.root = Path(self.temporary.name) / self.id().rsplit('.', 1)[1]
        self.state = self.root / 'state'
        self.reports = []

    def install(self, folder, installed='0.40-1', missing=(), pacman=None, **options):
        return update.install(folder, self.reports.append, self.keys.keyring, self.state, pacman or Pacman(), NOW,
                              lambda: installed, lambda package: list(missing), self.root / 'no.lck', **options)

    def test_installs_a_staged_copy_and_keeps_it_for_the_next_update(self):
        pacman = Pacman(snapper=True)
        version = self.install(bundle(self.root / 'one', self.keys), pacman=pacman)
        self.assertEqual(version, '0.41-1')
        command = [c for c in pacman.calls if c[:2] == ['pacman', '-U']][0]
        # pacman installs a link to the verified root-owned copy, not the file on the user's disk,
        # with no .sig beside it (pacman would check ours against its own keyring and refuse).
        self.assertTrue(command[3].startswith(str(self.state / 'pacman')))
        self.assertEqual(pacman.seen, [(True, False)])
        self.assertEqual(list((self.state / 'pacman').iterdir()), [])
        self.assertEqual([r['step'] for r in self.reports], ['copy', 'verify', 'install'])
        self.assertEqual([c[3] for c in pacman.calls if c[0] == 'snapper'], ['pre', 'post'])
        self.assertTrue((self.state / 'current/platinum-reality-0.41-1-x86_64.pkg.tar.zst').is_file())
        self.assertEqual(list((self.state / 'staging').iterdir()), [])
        # The next update moves it to "previous", and Restore Previous Version reinstalls it.
        self.install(bundle(self.root / 'two', self.keys, version='0.42-1'), installed='0.41-1')
        self.assertTrue((self.state / 'previous/platinum-reality-0.41-1-x86_64.pkg.tar.zst').is_file())
        pacman = Pacman()
        update.restore_previous(self.reports.append, self.keys.keyring, self.state, pacman)
        self.assertTrue(pacman.calls[-1][3].endswith('platinum-reality-0.41-1-x86_64.pkg.tar.zst'))

    def test_missing_dependencies_stop_before_pacman_runs(self):
        pacman = Pacman()
        with self.assertRaisesRegex(update.UpdateError, r'not installed \(sdl3\).*pacman -Syu'):
            self.install(bundle(self.root / 'deps', self.keys), missing=['sdl3'], pacman=pacman)
        self.assertFalse(any(c[:2] == ['pacman', '-U'] for c in pacman.calls))
        self.assertFalse((self.state / 'current').exists())

    def test_a_failed_pacman_run_is_reported_and_nothing_is_kept(self):
        with self.assertRaisesRegex(update.UpdateError, 'conflicting files'):
            self.install(bundle(self.root / 'fail', self.keys), pacman=Pacman(fail=True))
        self.assertFalse((self.state / 'current').exists())

    def test_a_busy_package_database_is_not_touched(self):
        (self.root).mkdir(parents=True, exist_ok=True)
        lock = self.root / 'db.lck'
        lock.write_text('')
        pacman = Pacman()
        with self.assertRaisesRegex(update.UpdateError, 'Another program'):
            update.install(bundle(self.root / 'busy', self.keys), self.reports.append, self.keys.keyring, self.state,
                           pacman, NOW, lambda: '0.40-1', lambda p: [], lock)
        self.assertEqual(pacman.calls, [])

    def test_nothing_to_restore_before_a_second_update(self):
        with self.assertRaisesRegex(update.UpdateError, 'No previous version'):
            update.restore_previous(self.reports.append, self.keys.keyring, self.state, Pacman())

    def test_staging_refuses_links_and_odd_package_names(self):
        folder = bundle(self.root / 'odd', self.keys, package='../evil.pkg.tar.zst')
        with self.assertRaisesRegex(update.UpdateError, 'unexpected package'):
            self.install(folder)


loader = importlib.machinery.SourceFileLoader(
    'software_update', str(Path(__file__).resolve().parents[1] / 'scripts/platinum-software-update'))
client = importlib.util.module_from_spec(importlib.util.spec_from_loader('software_update', loader))
loader.exec_module(client)


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.keys = Keys(cls.temporary.name, 'release')
        cls.other = Keys(cls.temporary.name, 'stranger')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_only_github_over_https(self):
        for url in ('https://api.github.com/x', 'https://github.com/psn-tch/platinum-reality/releases/download/a/b',
                    'https://objects.githubusercontent.com/x', 'https://release-assets.githubusercontent.com/x'):
            self.assertEqual(client.allowed(url), url)
        for url in ('http://github.com/x', 'https://github.com.evil.example/x', 'https://example.com/x',
                    'file:///etc/passwd', 'https://api.github.com@evil.example/x'):
            with self.assertRaises(client.platinum_update.UpdateError):
                client.allowed(url)

    def test_check_picks_the_newest_genuine_release(self):
        cache = Path(self.temporary.name) / 'cache'
        sources = {'v0.41': bundle(Path(self.temporary.name) / 'v0.41', self.keys, version='0.41-1'),
                   'v0.43': bundle(Path(self.temporary.name) / 'v0.43', self.other, version='0.43-1'),
                   'v0.42': bundle(Path(self.temporary.name) / 'v0.42', self.keys, version='0.42-1')}
        listing = [(tag, {name: f'{tag}/{name}' for name in ('release.json', 'release.json.sig')})
                   for tag in sources]
        said = []
        original = (client.releases, client.fetch, client.cache, client.say, client.platinum_update.installed_version,
                    client.platinum_update.parse_time)
        try:
            client.releases = lambda: listing
            client.fetch = lambda url, *a, **k: (sources[url.split('/')[0]] / url.split('/')[1]).read_bytes()
            client.cache = lambda: cache.mkdir(exist_ok=True) or cache
            client.say = said.append
            client.platinum_update.installed_version = lambda name='platinum-reality': '0.40-1'
            # The fixtures' descriptions are valid until NOW + 180 days; check against today's clock.
            client.check(self.keys.keyring)
        finally:
            (client.releases, client.fetch, client.cache, client.say, client.platinum_update.installed_version,
             client.platinum_update.parse_time) = original
        # v0.43 is newer but signed by a stranger: ignored. v0.42 is offered.
        self.assertEqual(said[-1]['available']['version'], '0.42-1')
        self.assertEqual(said[-1]['available']['tag'], 'v0.42')
        self.assertEqual(said[-1]['installed'], '0.40-1')

    def test_unusable_release_names_are_refused(self):
        for tag in ('', '../x', '.hidden', 'a/b'):
            with self.assertRaises(client.platinum_update.UpdateError):
                client.folder_for(tag)


if __name__ == '__main__':
    unittest.main()
