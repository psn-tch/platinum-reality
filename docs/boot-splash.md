# The startup screen

`scripts/boot-splash.py` replaces the boot messages with a gray 1-bit screen: the 512 × 342 screen of
the first Macintosh, scaled by a whole number, with a smiling computer where that screen had its
startup icon. See the README for installing and removing it.

## Unlocking an encrypted disk

When the disk is encrypted, the startup screen asks for the password itself. Platinum Reality's
installer encrypts by default. The prompt is a Macintosh dialog below the icon (added 2026-10-30):
- "Enter your password to start up." over an empty field;
- one bullet per character typed, up to 28;
- "Wrong password. Please try again." whenever the password is asked for again after one was typed.

The text is drawn at render time in an original pixel font that has only the letters these two
messages need. That means it can be turned with the picture for a display mounted sideways. Plymouth
can draw text, but not turned. The dialog appears in the theme script's password callbacks
(`packaging/plymouth/platinum-reality.script.in`). `boot-splash.py install` fills in its layout
numbers from `DIALOG`, `FIELD` and `BULLET_*`.

Image builders such as the live ISO must use `boot-splash.py theme --from RENDERED --output DIR`, never
fill the script themselves. It writes the whole theme with every number filled in and changes nothing
else. The ISO once filled in only the four older numbers and would have shipped a broken script.

The `plymouth` boot hook goes right after `kms`, so it runs before `sd-encrypt` or `encrypt` asks for
the password.

Known limits:
- The dialog always says "password", whatever the program asking for it wrote.
- With two encrypted disks, the second disk's prompt would also say "Wrong password". The installer
  makes one.

## Seeing it without a reboot

`tests/plymouth_preview.sh` runs the real `plymouthd` with the theme and saves screenshots: the
splash, the dialog, the typed bullets, after Return, and the wrong-password dialog. It also checks
that plymouthd returned both typed passwords. Run it only inside the headless harness, which it
checks for:

```bash
python3 scripts/with-headless-display.py --scale 1.0 --evidence /tmp/pv/harness -- \
    bash tests/plymouth_preview.sh /tmp/pv/out 1120x760 3     # size, quarter turns
```

plymouthd runs under bubblewrap as a namespace-only root with a private `/dev`, drawing through
its X11 renderer into a private rootful Xwayland. Keys are typed through XTEST
(`tests/plymouth_xtype.py`). Never loosen the sandbox: when only `/` was bound, plymouthd opened
the machine's real `/dev/tty1`, `/dev/dri/card1` and keyboards. The kernel refused it control
because it wasn't really root, but the preview must not be able to reach them at all. The script
refuses to start plymouthd if any console, graphics card or input device is visible.

Tests: `tests/boot_splash_tests.py` covers the pixel font, the layout, a bullet that looks the
same every way up, the filled script and the hook order.
