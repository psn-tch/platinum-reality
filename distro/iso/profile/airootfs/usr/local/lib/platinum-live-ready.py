#!/usr/bin/env python3
"""Live-only boot receipt on the VM serial console; no network or input control."""
import json
from pathlib import Path
import time

runs = Path('/home/liveuser/.local/state/platinum-reality/session-runs')
deadline = time.monotonic() + 180
while time.monotonic() < deadline:
    for path in sorted(runs.glob('*/session.json'), reverse=True):
        try:
            if path.is_symlink() or path.stat().st_size > 262144:
                continue
            state = json.loads(path.read_text())
            if state.get('ready') is True:
                print('PLATINUM_LIVE_READY ' + json.dumps({
                    'ready': True, 'uptime_seconds': float(Path('/proc/uptime').read_text().split()[0]),
                    'source': 'packaged supervisor session.json'}), flush=True)
                raise SystemExit(0)
        except (OSError, ValueError):
            continue
    time.sleep(.5)
print('PLATINUM_LIVE_FAILED no ready session within 180 seconds', flush=True)
raise SystemExit(1)
