#!/usr/bin/env bash
# Physical-seat candidate; never changes the existing tty1 startup or login gate.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Intentionally pin the core to the already-qualified release, rather than
# running whatever unqualified development build happens to be present.
launcher="${PLATINUM_SESSION_LAUNCHER:-$project_dir/build/packages/20261018T203329/extracted/usr/bin/platinum-reality}"
state_root="${XDG_STATE_HOME:-$HOME/.local/state}/platinum-reality/session-runs"
evidence="$state_root/$(date -u +%Y%m%dT%H%M%SZ)-$$"
exec python3 "$project_dir/scripts/standalone-session.py" \
    --launcher "$launcher" --evidence "$evidence" "$@"
