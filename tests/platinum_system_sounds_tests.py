#!/usr/bin/env python3
"""Mac sounds only: removing the Arch/KDE desktop sounds and getting them back, on a throwaway root with
Arch's own pacman.conf. pacman-conf itself reads every changed file; pacman -S is a stand-in."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('platinum_system_sounds', HERE.parent / 'scripts/platinum_system_sounds.py')
sounds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sounds)
STOCK = (HERE / 'fixtures/pacman/pacman.conf').read_text()
# Lily Hewitt's laptop and potato: remove-arch-sounds.sh's lines, before the control panel existed.
SCRIPT_LINES = ('# Platinum Reality: no Arch/KDE desktop sounds (Lily Hewitt, 2026-11-12)\n'
                'NoExtract   = usr/share/sounds/freedesktop/* usr/share/sounds/ocean/* usr/share/sounds/oxygen/* '
                'usr/share/sounds/Oxygen-* usr/share/sounds/plasma-bigscreen/*\n')


class Runner:
    """pacman-conf runs for real; pacman -S is recorded (and can fail)."""

    def __init__(self, fail=''):
        self.installs = []
        self.fail = fail

    def __call__(self, command, **arguments):
        if command[0] == 'pacman-conf':
            return subprocess.run(command, **arguments)
        self.installs.append(command)
        return subprocess.CompletedProcess(command, 1 if self.fail else 0, '', self.fail)


class SystemSoundsTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        (self.root / 'etc').mkdir()
        (self.root / sounds.CONF).write_text(STOCK)
        folder = self.root / sounds.SOUNDS
        for name in ('freedesktop/stereo', 'ocean/stereo', 'oxygen/stereo', 'alsa', 'Platinum'):
            (folder / name).mkdir(parents=True)
            (folder / name / 'bell.oga').write_bytes(b'ogg')
        (folder / 'Oxygen-Sys-Log-In.ogg').write_bytes(b'ogg')
        self.package('sound-theme-freedesktop', 'usr/share/sounds/freedesktop/stereo/bell.oga')
        self.package('ocean-sound-theme', 'usr/share/sounds/ocean/stereo/bell.oga')
        self.package('oxygen-sounds', 'usr/share/sounds/Oxygen-Sys-Log-In.ogg')
        self.package('alsa-utils', 'usr/share/sounds/alsa/Front_Center.wav')
        self.package('bash', 'usr/bin/bash')

    def tearDown(self):
        self.folder.cleanup()

    def package(self, name, path):
        entry = self.root / sounds.LOCAL_DB / f'{name}-1-1'
        entry.mkdir(parents=True)
        (entry / 'desc').write_text(f'%NAME%\n{name}\n\n%VERSION%\n1-1\n\n')
        (entry / 'files').write_text(f'%FILES%\n{path}\n\n')

    def conf(self):
        return (self.root / sounds.CONF).read_text()

    def no_extract(self, path=None):
        process = subprocess.run(['pacman-conf', '--config', str(path or self.root / sounds.CONF), 'NoExtract'],
                                 capture_output=True, text=True, check=True)
        return process.stdout.split()

    def test_status_on_a_stock_system(self):
        self.assertEqual(sounds.status(self.root), {
            'removed': False, 'present': ['Oxygen-Sys-Log-In.ogg', 'freedesktop', 'ocean', 'oxygen'],
            'packages': ['ocean-sound-theme', 'oxygen-sounds', 'sound-theme-freedesktop']})

    def test_remove_deletes_only_the_desktop_sounds_and_keeps_them_out_of_updates(self):
        sounds.remove(self.root, Runner())
        left = sorted(p.name for p in (self.root / sounds.SOUNDS).iterdir())
        self.assertEqual(left, ['Platinum', 'alsa'])  # speaker-test's voices and our own stay.
        self.assertEqual(self.no_extract(), sounds.PATTERNS)
        # In [options], right after its #NoExtract example; nothing else in the file changed.
        lines = self.conf().splitlines()
        at = lines.index('#NoExtract   =')
        self.assertTrue(lines[at + 1].startswith(sounds.MARK))
        self.assertEqual(lines[at + 2], sounds.NO_EXTRACT)
        self.assertEqual('\n'.join(lines[:at + 1] + lines[at + 3:]) + '\n', STOCK)
        self.assertEqual((self.root / 'etc/pacman.conf.before-sounds').read_text(), STOCK)
        self.assertTrue(sounds.status(self.root)['removed'])
        self.assertEqual(sounds.status(self.root)['present'], [])

    def test_restore_takes_the_line_out_and_downloads_the_owners_again(self):
        sounds.remove(self.root, Runner())
        run = Runner()
        sounds.restore(self.root, run)
        self.assertEqual(self.conf(), STOCK)
        self.assertEqual(self.no_extract(), [])
        self.assertEqual(run.installs, [['pacman', '-S', '--noconfirm', '--noprogressbar', '--color', 'never',
                                         'ocean-sound-theme', 'oxygen-sounds', 'sound-theme-freedesktop']])
        self.assertFalse(sounds.status(self.root)['removed'])

    def test_the_first_scripts_lines_are_recognized_and_removed(self):
        stock = STOCK.replace('#NoExtract   =\n', '#NoExtract   =\n' + SCRIPT_LINES)
        (self.root / sounds.CONF).write_text(stock)
        self.assertTrue(sounds.removed(self.root))
        sounds.remove(self.root, Runner())  # Already there: not added twice.
        self.assertEqual(self.conf(), stock)
        sounds.restore(self.root, Runner())
        self.assertEqual(self.conf(), STOCK)

    def test_someone_elses_no_extract_is_kept(self):
        mine = STOCK.replace('#NoExtract   =\n', '#NoExtract   =\nNoExtract = usr/share/help/*\n')
        (self.root / sounds.CONF).write_text(mine)
        sounds.remove(self.root, Runner())
        self.assertEqual(sorted(self.no_extract()), sorted(['usr/share/help/*'] + sounds.PATTERNS))
        sounds.restore(self.root, Runner())
        self.assertEqual(self.conf(), mine)

    def test_without_an_example_line_it_goes_at_the_end_of_options(self):
        stock = STOCK.replace('#NoExtract   =\n', '')
        (self.root / sounds.CONF).write_text(stock)
        sounds.remove(self.root, Runner())
        self.assertEqual(self.no_extract(), sounds.PATTERNS)
        lines = self.conf().splitlines()
        self.assertLess(lines.index(sounds.NO_EXTRACT), lines.index('[core]'))
        sounds.restore(self.root, Runner())
        self.assertEqual(self.conf(), stock)

    def test_a_file_pacman_cannot_read_is_never_written(self):
        broken = STOCK.replace('[options]', '[options\n', 1)
        (self.root / sounds.CONF).write_text(broken)
        with self.assertRaises(sounds.SoundsError):
            sounds.remove(self.root, Runner())
        self.assertEqual(self.conf(), broken)
        self.assertIn('freedesktop', sounds.present(self.root))  # Nothing deleted either.
        self.assertEqual(sorted(p.name for p in (self.root / 'etc').iterdir()), ['pacman.conf'])

    def test_a_failed_download_says_the_sounds_come_back_with_the_next_update(self):
        sounds.remove(self.root, Runner())
        with self.assertRaisesRegex(sounds.SoundsError, 'Update All Software'):
            sounds.restore(self.root, Runner(fail='error: failed retrieving file'))
        self.assertEqual(self.conf(), STOCK)  # The line is out, so they do come back.

    def test_a_symlinked_sound_folder_is_unlinked_not_followed(self):
        target = self.root / 'elsewhere'
        target.mkdir()
        (target / 'keep.oga').write_bytes(b'ogg')
        shutil.rmtree(self.root / sounds.SOUNDS / 'ocean')
        (self.root / sounds.SOUNDS / 'ocean').symlink_to(target)
        sounds.remove(self.root, Runner())
        self.assertTrue((target / 'keep.oga').exists())
        self.assertFalse((self.root / sounds.SOUNDS / 'ocean').exists())


class HelperTests(unittest.TestCase):
    def helper(self, *arguments):
        process = subprocess.run([sys.executable, '-B', str(HERE.parent / 'scripts/platinum-system-sounds'), *arguments],
                                 capture_output=True, text=True,
                                 env={**os.environ, 'PYTHONPATH': str(HERE.parent / 'scripts')})
        return process.returncode, [json.loads(line) for line in process.stdout.splitlines()]

    def test_status_runs_as_anyone(self):
        code, lines = self.helper('status')
        self.assertEqual(code, 0)
        self.assertEqual(set(lines[0]), {'removed', 'present', 'packages'})

    def test_unknown_arguments_are_refused(self):
        self.assertEqual(self.helper('remove', '/')[0], 2)

    @unittest.skipIf(os.geteuid() == 0, 'runs as root')
    def test_changing_needs_root(self):
        for argument in ('remove', 'restore'):
            code, lines = self.helper(argument)
            self.assertEqual(code, 1)
            self.assertIn('administrator', lines[0]['error'])


if __name__ == '__main__':
    unittest.main()
