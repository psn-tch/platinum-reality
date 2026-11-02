#!/usr/bin/env python3
"""Repair only the supplied cloud builder's legacy GRUB kernel path after -Syu.

Current Arch stores the kernel in /usr/lib/modules; this cloud base's GRUB
expects /boot/vmlinuz-linux. This is NOT an installed-target boot strategy.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess

if (os.geteuid() != 0 or socket.gethostname() != 'pr-iso-builder' or
        subprocess.check_output(['systemd-detect-virt', '--vm'], text=True).strip() not in ('qemu', 'kvm')):
    raise SystemExit('Requires root inside the copied QEMU builder only.')
kernels = [path.parent/'vmlinuz' for path in Path('/usr/lib/modules').glob('*/pkgbase')
           if path.read_text().strip() == 'linux' and (path.parent/'vmlinuz').is_file()]
if len(kernels) != 1:
    raise SystemExit('Cannot identify the one installed linux package kernel.')
shutil.copyfile(kernels[0], '/boot/vmlinuz-linux')
os.chmod('/boot/vmlinuz-linux', 0o644)
print(json.dumps({'builder_only_legacy_grub_kernel': str(kernels[0]),
                  'destination': '/boot/vmlinuz-linux'}))
