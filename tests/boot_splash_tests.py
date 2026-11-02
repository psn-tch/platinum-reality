"""The boot screen tool against a fake root and home: nothing here touches the real boot setup."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
TOOL = Path(__file__).resolve().parents[1] / 'scripts/boot-splash.py'
spec = importlib.util.spec_from_file_location('boot_splash', TOOL)
splash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(splash)


def tool(*arguments, home=None):
    environment = {'PATH': '/usr/bin:/bin', 'HOME': str(home or tempfile.gettempdir())}
    return subprocess.run([sys.executable, '-B', str(TOOL), *arguments], env=environment,
                          capture_output=True, text=True)


class BootSplashTests(unittest.TestCase):
    def test_the_panel_picture_is_the_mac_screen_scaled_turned_and_in_step(self):
        # A made-up 2 x 2 icon: black, white / white, transparent.
        icon = ([[0, 255], [255, 0]], [[255, 255], [255, 0]])
        rows, scale = splash.panel_picture(1600, 2560, 3, icon)
        self.assertEqual((len(rows[0]), len(rows), scale), (1600, 2560, 4))
        upright = splash.rotate(rows, 1)          # As the panel is held.
        self.assertEqual((len(upright[0]), len(upright)), (2560, 1600))
        origin = ((2560 - 512 * 4) // 2, (1600 - 342 * 4) // 2)
        at = splash.icon_position(icon)
        self.assertEqual(at, (255, 160))  # Centred where the Mac's icon was.
        left, top = origin[0] + at[0] * 4, origin[1] + at[1] * 4
        self.assertEqual(upright[top][left], 0)            # The icon's black pixel, 4 x 4.
        self.assertEqual(upright[top + 3][left + 3], 0)
        self.assertEqual(upright[top][left + 4], 255)      # Its white pixel.
        # Its transparent pixel shows the checkerboard, in step with the Mac screen's corner.
        unit = (at[0] + 1, at[1] + 1)
        self.assertEqual(upright[top + 4][left + 4], 0 if (unit[0] + unit[1]) % 2 == 0 else 255)
        self.assertEqual(upright[origin[1]][origin[0]], 0)  # The Mac screen's own (0, 0) is black.
        self.assertEqual(upright[origin[1]][origin[0] + 4], 255)
        # Fifty percent gray overall.
        black = sum(row.count(0) for row in upright[:400])
        self.assertAlmostEqual(black / (400 * 2560), 0.5, places=2)

    def test_the_unlock_dialog_is_drawn_in_the_pixel_font_and_fits_the_mac_screen(self):
        # An encrypted disk asks for its password in a Macintosh dialog (docs/boot-splash.md).
        for message in (splash.PROMPT, splash.RETRY):
            self.assertTrue(set(message) <= set(splash.GLYPHS), message)
            rows = splash.dialog_picture(message)
            self.assertEqual((len(rows[0]), len(rows)), splash.DIALOG[2:])
            text = splash.text_rows(message)
            self.assertLess(splash.MESSAGE_AT[0] + len(text[0]), splash.FIELD[0] + splash.FIELD[2])
            self.assertLess(splash.MESSAGE_AT[1] + len(text), splash.FIELD[1])
        for glyph in splash.GLYPHS.values():
            self.assertIn(len(glyph), (7, 9))
            self.assertEqual(len(set(map(len, glyph))), 1)
        # The two messages differ, and the field and frame are identical in both.
        prompt, retry = splash.dialog_picture(splash.PROMPT), splash.dialog_picture(splash.RETRY)
        self.assertNotEqual(prompt, retry)
        self.assertEqual(prompt[splash.FIELD[1]:], retry[splash.FIELD[1]:])
        # Below the startup icon, inside the 512 x 342 screen, with every bullet inside the field.
        x, y, w, h = splash.DIALOG
        self.assertTrue(0 <= x and x + w <= 512 and y > splash.ICON_CENTRE[1] + 20 and y + h <= 342)
        last = splash.BULLET_AT[0] + (splash.BULLET_MAX - 1) * splash.BULLET_STEP + splash.BULLET
        self.assertLessEqual(last, splash.FIELD[0] + splash.FIELD[2] - 4)
        self.assertLessEqual(splash.BULLET_AT[1] + splash.BULLET, splash.FIELD[1] + splash.FIELD[3] - 4)
        # A bullet looks the same every way up, so one picture serves a panel mounted sideways.
        bullet, mask = splash.bullet_picture()
        self.assertEqual(splash.rotate(bullet, 1), bullet)
        self.assertEqual(splash.rotate(mask, 1), mask)

    def test_the_boot_hook_runs_before_the_disk_is_unlocked(self):
        # The dialog can only answer a password request if Plymouth starts before sd-encrypt or encrypt.
        for hooks in ('base systemd autodetect microcode modconf kms keyboard sd-vconsole block sd-encrypt filesystems fsck',
                      'base udev autodetect microcode modconf kms keyboard keymap consolefont block encrypt filesystems fsck'):
            with tempfile.TemporaryDirectory() as root:
                (Path(root) / 'etc/mkinitcpio.conf.d').mkdir(parents=True)
                (Path(root) / 'etc/mkinitcpio.conf').write_text(f'HOOKS=({hooks})\n')
                result = splash.mkinitcpio_hooks(root)
                unlock = 'sd-encrypt' if 'sd-encrypt' in result else 'encrypt'
                self.assertLess(result.index('plymouth'), result.index(unlock))
                self.assertEqual(result.index('plymouth'), result.index('kms') + 1)

    def test_an_image_builder_gets_the_whole_theme_from_one_command(self):
        # The live ISO filled the script itself and missed today's dialog numbers (2026-10-30):
        # image builders use `theme`, which fills every number as install does.
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            result = tool('render', '--output', str(work / 'r'), '--size', '1024x768', '--rotate', '0',
                          '--cmdline', '/dev/null', home=work)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = tool('theme', '--from', str(work / 'r'), '--output', str(work / 't'), home=work)
            self.assertEqual(result.returncode, 0, result.stderr)
            script = (work / 't/platinum-reality.script').read_text()
            self.assertNotIn('@', script)
            self.assertIn('Plymouth.SetDisplayPasswordFunction', script)
            # Every rendered picture, the last bullet too (RENDERED[:-1] dropped it; Rocky, 2026-10-30).
            self.assertEqual(set(splash.PICTURES), set(splash.RENDERED) - {'manifest.json'})
            for name in splash.PICTURES + ['platinum-reality.plymouth']:
                self.assertTrue((work / 't' / name).is_file(), name)
            self.assertFalse((work / 't/manifest.json').exists())
            # A folder that already has something in it is refused.
            self.assertNotEqual(tool('theme', '--from', str(work / 'r'), '--output', str(work / 't'),
                                     home=work).returncode, 0)

    def test_the_theme_script_gets_every_number(self):
        script = (TOOL.parents[1] / 'packaging/plymouth/platinum-reality.script.in').read_text()
        filled = splash.fill_script(script, {'PANEL_WIDTH': 1600, 'PANEL_HEIGHT': 2560, 'ICON_X': 243,
                                             'ICON_Y': 145, 'PANEL_ROTATION': 3, 'PANEL_SCALE': 4})
        self.assertNotIn('@', filled)
        self.assertIn('panel_rotation = 3;', filled)
        self.assertIn(f'bullet_max = {splash.BULLET_MAX};', filled)
        self.assertIn('Plymouth.SetDisplayPasswordFunction(show_password);', filled)
        with self.assertRaises(SystemExit):
            splash.fill_script(script, {'PANEL_WIDTH': 1})

    def test_render_install_and_uninstall_in_a_fake_root(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            home, root, output = work / 'home', work / 'root', work / 'rendered'
            (home / '.local/share/platinum-reality/assets').mkdir(parents=True)
            subprocess.run(['magick', '-size', '3x4', 'xc:white', '-fill', 'black', '-draw', 'point 1,1',
                            str(home / '.local/share/platinum-reality/assets/happy-mac.png')], check=True)
            cmdline = work / 'cmdline'
            cmdline.write_text('root=PARTUUID=x rw fbcon=rotate:1\n')
            result = tool('render', '--output', str(output), '--size', '800x1280', '--cmdline', str(cmdline), home=home)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest, {'panel_width': 800, 'panel_height': 1280, 'rotation': 1, 'scale': 2,
                                        'icon': 'user', 'icon_x': 254, 'icon_y': 159})
            self.assertEqual(subprocess.run(['magick', 'compare', '-metric', 'AE', str(output / 'panel.png'),
                                             str(output / 'splash.bmp'), 'null:'], capture_output=True, text=True).stderr.split()[0], '0')

            (root / 'etc/kernel').mkdir(parents=True)
            (root / 'etc/kernel/cmdline').write_text('root=PARTUUID=x rw fbcon=rotate:1 quiet\n')
            (root / 'etc/mkinitcpio.d').mkdir(parents=True)
            preset = 'ALL_kver="/boot/vmlinuz-linux"\ndefault_uki="/boot/EFI/Linux/arch-linux.efi"\n' \
                     'default_options="--splash /usr/share/systemd/bootctl/splash-arch.bmp"\n'
            (root / 'etc/mkinitcpio.d/linux.preset').write_text(preset)
            (root / 'etc/mkinitcpio.conf').write_text('HOOKS=(base udev autodetect microcode modconf kms keyboard block filesystems fsck)\n')
            (root / 'etc/mkinitcpio.conf.d').mkdir()
            (root / 'usr/bin').mkdir(parents=True)
            (root / 'usr/bin/plymouth').write_text('')
            originals = {p: (root / p).read_bytes() for p in ('etc/kernel/cmdline', 'etc/mkinitcpio.d/linux.preset')}

            result = tool('install', '--root', str(root), '--no-rebuild', '--from', str(output), home=home)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('+ plymouth-set-default-theme platinum-reality', result.stdout)
            self.assertIn('+ mkinitcpio -P', result.stdout)
            self.assertEqual((root / 'etc/kernel/cmdline').read_text(),
                             'root=PARTUUID=x rw fbcon=rotate:1 quiet splash loglevel=0 rd.udev.log_level=3 '
                             'vt.global_cursor_default=0 plymouth.force-scale=1\n')
            self.assertIn('--splash /usr/share/plymouth/themes/platinum-reality/splash.bmp',
                          (root / 'etc/mkinitcpio.d/linux.preset').read_text())
            self.assertNotIn('splash-arch', (root / 'etc/mkinitcpio.d/linux.preset').read_text())
            # The quote after the path stays (psn_tch's first install lost it, 2026-10-22).
            self.assertIn('default_options="--splash /usr/share/plymouth/themes/platinum-reality/splash.bmp"\n',
                          (root / 'etc/mkinitcpio.d/linux.preset').read_text())
            self.assertIn('HOOKS=(base udev autodetect microcode modconf kms plymouth keyboard',
                          (root / 'etc/mkinitcpio.conf.d/platinum-reality-plymouth.conf').read_text())
            theme = root / 'usr/share/plymouth/themes/platinum-reality'
            script = (theme / 'platinum-reality.script').read_text()
            self.assertIn('panel_width = 800;', script)
            self.assertIn('panel_height = 1280;', script)
            self.assertIn('icon_x = 254;', script)
            self.assertIn('icon_y = 159;', script)
            self.assertNotIn('@', script)
            for name in ['platinum-reality.plymouth', 'panel.png', 'splash.bmp', 'tile-4.png', 'icon-4.png']:
                self.assertTrue((theme / name).is_file(), name)
            # Installing twice keeps the first backups and adds nothing twice.
            self.assertEqual(tool('install', '--root', str(root), '--no-rebuild', '--from', str(output)).returncode, 0)
            self.assertEqual((root / 'etc/kernel/cmdline').read_text().count('splash'), 1)

            result = tool('uninstall', '--root', str(root), '--no-rebuild')
            self.assertEqual(result.returncode, 0, result.stderr)
            for path, data in originals.items():
                self.assertEqual((root / path).read_bytes(), data, path)
            self.assertFalse((root / 'etc/mkinitcpio.conf.d/platinum-reality-plymouth.conf').exists())
            self.assertFalse(theme.exists())

    def test_a_screen_installed_as_platinum_dreams_is_taken_over_and_still_uninstalls(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            home, root, output = work / 'home', work / 'root', work / 'rendered'
            # The old name's assets folder becomes the new one, and the drawing still finds the icon.
            (home / '.local/share/platinum-dreams/assets').mkdir(parents=True)
            subprocess.run(['magick', '-size', '3x4', 'xc:white', '-fill', 'black', '-draw', 'point 1,1',
                            str(home / '.local/share/platinum-dreams/assets/happy-mac.png')], check=True)
            cmdline = work / 'cmdline'
            cmdline.write_text('root=PARTUUID=x rw\n')
            result = tool('render', '--output', str(output), '--size', '800x1280', '--cmdline', str(cmdline), home=home)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads((output / 'manifest.json').read_text())['icon'], 'user')
            self.assertTrue((home / '.local/share/platinum-reality/assets/happy-mac.png').is_file())
            self.assertTrue((home / '.local/share/platinum-dreams').is_symlink())
            # As an install under the old name left a machine: originals backed up, edited files in place.
            original_cmdline, original_preset = 'root=PARTUUID=x rw\n', 'default_options="--splash /x/splash-arch.bmp"\n'
            for path, text in (('etc/kernel/cmdline', 'root=PARTUUID=x rw quiet splash\n'),
                               ('etc/mkinitcpio.d/linux.preset',
                                'default_options="--splash /usr/share/plymouth/themes/platinum-dreams/splash.bmp"\n'),
                               ('etc/mkinitcpio.conf.d/platinum-dreams-plymouth.conf', 'HOOKS=(base udev kms plymouth)\n'),
                               ('etc/mkinitcpio.conf', 'HOOKS=(base udev kms block)\n'),
                               ('usr/share/plymouth/themes/platinum-dreams/splash.bmp', 'old'),
                               ('var/lib/platinum-dreams/boot-splash/cmdline', original_cmdline),
                               ('var/lib/platinum-dreams/boot-splash/linux.preset', original_preset),
                               ('usr/bin/plymouth', '')):
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text)
            result = tool('install', '--root', str(root), '--no-rebuild', '--from', str(output), home=home)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((root / 'usr/share/plymouth/themes/platinum-dreams').exists())
            self.assertFalse((root / 'etc/mkinitcpio.conf.d/platinum-dreams-plymouth.conf').exists())
            self.assertFalse((root / 'var/lib/platinum-dreams').exists())
            self.assertEqual((root / 'var/lib/platinum-reality/boot-splash/cmdline').read_text(), original_cmdline)
            self.assertIn('--splash /usr/share/plymouth/themes/platinum-reality/splash.bmp"',
                          (root / 'etc/mkinitcpio.d/linux.preset').read_text())
            result = tool('uninstall', '--root', str(root), '--no-rebuild')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'etc/kernel/cmdline').read_text(), original_cmdline)
            self.assertEqual((root / 'etc/mkinitcpio.d/linux.preset').read_text(), original_preset)
            self.assertFalse((root / 'usr/share/plymouth/themes/platinum-reality').exists())
        with tempfile.TemporaryDirectory() as work:
            # Uninstalling straight from an old-name install restores the originals too.
            root = Path(work)
            for path, text in (('etc/kernel/cmdline', 'edited\n'),
                               ('etc/mkinitcpio.conf.d/platinum-dreams-plymouth.conf', 'HOOKS=(base)\n'),
                               ('var/lib/platinum-dreams/boot-splash/cmdline', 'original\n')):
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text)
            result = tool('uninstall', '--root', str(root), '--no-rebuild')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'etc/kernel/cmdline').read_text(), 'original\n')
            self.assertFalse((root / 'etc/mkinitcpio.conf.d/platinum-dreams-plymouth.conf').exists())

    def test_a_broken_preset_is_repaired_and_a_failed_rebuild_puts_everything_back(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            output, root = work / 'rendered', work / 'root'
            self.assertEqual(tool('render', '--output', str(output), '--size', '1024x768', '--rotate', '0', home=work).returncode, 0)
            (root / 'etc/kernel').mkdir(parents=True)
            (root / 'etc/mkinitcpio.d').mkdir(parents=True)
            (root / 'etc/mkinitcpio.conf.d').mkdir(parents=True)
            (root / 'usr/bin').mkdir(parents=True)
            (root / 'usr/bin/plymouth').write_text('')
            (root / 'etc/mkinitcpio.conf').write_text('HOOKS=(base udev kms block filesystems)\n')
            original_preset = 'default_uki="/boot/EFI/Linux/arch-linux.efi"\n' \
                              'default_options="--splash /usr/share/systemd/bootctl/splash-arch.bmp"\n'
            original_cmdline = 'root=x rw\n'
            # As the first install left psn_tch's machine: originals backed up, the preset broken.
            backups = root / 'var/lib/platinum-reality/boot-splash'
            backups.mkdir(parents=True)
            (backups / 'linux.preset').write_text(original_preset)
            (backups / 'cmdline').write_text(original_cmdline)
            (root / 'etc/mkinitcpio.d/linux.preset').write_text(
                'default_uki="/boot/EFI/Linux/arch-linux.efi"\n'
                'default_options="--splash /usr/share/plymouth/themes/platinum-reality/splash.bmp\n')
            (root / 'etc/kernel/cmdline').write_text(original_cmdline.strip() + ' quiet splash\n')
            # A rebuild that fails: everything goes back.
            failing = work / 'mkinitcpio'
            failing.write_text('#!/bin/sh\nexit 1\n')
            failing.chmod(0o755)
            fake_theme = work / 'plymouth-set-default-theme'
            fake_theme.write_text('#!/bin/sh\nexit 0\n')
            fake_theme.chmod(0o755)
            environment = {'PATH': f'{work}:/usr/bin:/bin', 'HOME': str(work)}
            result = subprocess.run([sys.executable, '-B', str(TOOL), 'install', '--root', str(root), '--from', str(output),
                                     '--mkinitcpio', str(failing)], env=environment, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('nothing changed', result.stderr)
            self.assertEqual((root / 'etc/mkinitcpio.d/linux.preset').read_text(), original_preset)
            self.assertEqual((root / 'etc/kernel/cmdline').read_text(), original_cmdline)
            self.assertFalse((root / 'etc/mkinitcpio.conf.d/platinum-reality-plymouth.conf').exists())
            # Installing again starts from the originals: a well-formed preset.
            self.assertEqual(tool('install', '--root', str(root), '--no-rebuild', '--from', str(output)).returncode, 0)
            preset = (root / 'etc/mkinitcpio.d/linux.preset').read_text()
            self.assertIn('default_options="--splash /usr/share/plymouth/themes/platinum-reality/splash.bmp"\n', preset)
            self.assertEqual(subprocess.run(['bash', '-n', str(root / 'etc/mkinitcpio.d/linux.preset')]).returncode, 0)
            self.assertEqual((root / 'etc/kernel/cmdline').read_text().split().count('splash'), 1)

    def test_install_refuses_incomplete_or_linked_renders(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            output, root = work / 'rendered', work / 'root'
            result = tool('render', '--output', str(output), '--size', '1024x768', '--rotate', '0', home=work)
            self.assertEqual(result.returncode, 0, result.stderr)
            # Without the user's own icon: the project's original drawing.
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['icon'], 'original')
            self.assertEqual((manifest['icon_x'], manifest['icon_y']), splash.icon_position(splash.original_icon()))
            (output / 'tile-3.png').unlink()
            (output / 'tile-3.png').symlink_to('/etc/passwd')
            root.mkdir()
            result = tool('install', '--root', str(root), '--no-rebuild', '--from', str(output))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('tile-3.png', result.stderr)
            self.assertFalse((root / 'usr/share/plymouth').exists())

    def test_a_happy_mac_sized_icon_sits_exactly_where_the_mac_put_it(self):
        icon = ([[255] * 25 for _ in range(32)], [[255] * 25 for _ in range(32)])
        self.assertEqual(splash.icon_position(icon), (243, 145))
        original = splash.original_icon()
        self.assertEqual({len(row) for row in original[0]}, {len(original[0][0])})

    def test_the_root_half_needs_root_and_the_drawing_half_refuses_it(self):
        if os.geteuid() == 0:
            self.skipTest('run as a user')
        self.assertIn('sudo', tool('uninstall').stderr)


if __name__ == '__main__':
    unittest.main()
