#!/usr/bin/env python3
"""R1 acceptance: owned OVMF VM, read-only ISO, no disk, no network, no input."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import sys
import time
import re

sys.path.insert(0, str(Path(__file__).resolve().parent))
from owned import Guest, Lease, sha256
from display_profiles import GPUS


def ready_marker(log):
    # UEFI may finish a terminal-capability query immediately before the service
    # writes its line, without an intervening newline. The marker remains an
    # exact token with a validated JSON body; do not depend on terminal layout.
    for match in re.finditer(r'PLATINUM_LIVE_READY ([^\r\n]*)', log):
        try:
            marker = json.loads(match.group(1))
            uptime = marker['uptime_seconds']
            if (marker.get('ready') is True and marker.get('source') == 'packaged supervisor session.json'
                    and type(uptime) in (int, float) and math.isfinite(uptime) and uptime >= 0):
                return marker
        except (ValueError, KeyError, TypeError):
            pass  # A concurrently read partial write is retried, never accepted.
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--gpu', choices=GPUS, default='virtio-vga')
    parser.add_argument('--outputs', type=int, choices=(1, 2), default=1)
    options = parser.parse_args()
    if os.geteuid() == 0:
        raise SystemExit('No host root. Run as the ordinary VM owner.')
    root = Path(__file__).resolve().parents[2]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    path = root/'build/distro'/('uefi-'+stamp)
    with Lease():
        path.mkdir(mode=0o700)
        guest = Guest(path)
        receipt = {'iso': options.iso.name, 'sha256': sha256(options.iso),
                   'firmware': 'OVMF UEFI', 'bios': 'NOT TESTED', 'network': False,
                   'writable_target_disk': False, 'manual_input': False,
                   'ready': False, 'visual_review': 'REQUIRED: view desktop.ppm after this command'}
        try:
            guest.start(iso=options.iso, uefi=True, network=False, gpu=options.gpu, outputs=options.outputs)
            receipt['display_profile'] = guest.display_profile
            receipt['qemu_version'] = guest.qmp('query-version')
            receipt['guest_output_activity'] = 'NOT VERIFIED: readiness and head capture alone do not establish multi-output usability'
            deadline = time.monotonic()+240
            next_capture = time.monotonic()+20
            while time.monotonic() < deadline:
                if guest.process.poll() is not None:
                    raise RuntimeError('Live guest exited before desktop readiness')
                try:
                    log = (path/'serial.log').read_text(errors='replace')
                except FileNotFoundError:
                    log = ''
                marker = ready_marker(log)
                if marker:
                    receipt.update(ready=True, boot_seconds=round(time.monotonic()-guest.started, 3),
                                   guest_uptime_seconds=marker['uptime_seconds'])
                    time.sleep(2)
                    guest.qmp('screendump', {'filename': str(path/'desktop.ppm')})
                    receipt['display_captures'] = guest.capture_displays('desktop-head')
                    print('Unassisted UEFI live desktop ready: '+str(path/'desktop.ppm'), flush=True)
                    break
                if time.monotonic() >= next_capture:
                    guest.qmp('screendump', {'filename': str(path/'progress.ppm')})
                    next_capture = time.monotonic()+20
                time.sleep(.5)
            else:
                guest.qmp('screendump', {'filename': str(path/'failed.ppm')})
                raise TimeoutError('Live desktop did not report ready; screenshots and serial retained')
        finally:
            guest.stop()
            (path/'acceptance.json').write_text(json.dumps(receipt, indent=2)+'\n')
            (path/'lease-finished.txt').write_text(datetime.now(timezone.utc).isoformat()+'\n')


if __name__ == '__main__':
    main()
