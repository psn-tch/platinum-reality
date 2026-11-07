#!/usr/bin/env python3
"""R3: one-command owned UEFI ISO → offline install → detached encrypted boot → password login.

No host root, real disks, user session changes or release selection. An actual
installer ISO built by distro/iso/build.py is required; the old R1 ISO lacks
the immutable live engine/wrapper and intentionally fails this gate.
"""
import argparse
import importlib.util
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--gui', action='store_true', help='R3b: drive the real unprivileged installer GUI instead of the headless install command')
    parser.add_argument('--repeat-login', action='store_true', help='Use installed Finder Restart and repeat encrypted/password login after the first desktop')
    parser.add_argument('--occupied-gui', action='store_true', help='Require the GUI to show an existing synthetic partition and its removal warning')
    parser.add_argument('--verify-erase-return', action='store_true', help='Require actual Return-to-Cancel with unchanged disk before the explicit erase action')
    parser.add_argument('--collect-boot-metrics', action='store_true', help='Retain read-only live systemd metrics and separate controller observations')
    parser.add_argument('--lease-wait', type=float, default=600)
    parser.add_argument('--greeter', choices=('stock', 'platinum'), default='stock')
    parser.add_argument('--unlock-ui', choices=('plain', 'plymouth'), default='plain')
    args = parser.parse_args()
    if args.repeat_login and not args.gui:
        parser.error('--repeat-login requires --gui')
    if args.occupied_gui and not args.gui:
        parser.error('--occupied-gui requires --gui')
    if args.verify_erase_return and not args.occupied_gui:
        parser.error('--verify-erase-return requires --occupied-gui')
    path = Path(__file__).with_name('install-proof.py')
    spec = importlib.util.spec_from_file_location('install_proof', path)
    proof = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(proof)
    proof.main([str(args.iso), '--image-engine', '--boot-proof', '--lease-wait', str(args.lease_wait),
                '--greeter', args.greeter, '--unlock-ui', args.unlock_ui] + (['--gui'] if args.gui else []) +
                (['--repeat-login'] if args.repeat_login else []) +
                (['--occupied-gui'] if args.occupied_gui else []) +
                (['--verify-erase-return'] if args.verify_erase_return else []) +
                (['--collect-boot-metrics'] if args.collect_boot_metrics else []))


if __name__ == '__main__':
    main()
