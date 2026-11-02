#!/usr/bin/env bash
# One host command; all installs, makepkg and mkarchiso run inside the copied VM.
set -euo pipefail
exec python3 -I "$(dirname -- "${BASH_SOURCE[0]}")/build.py" "$@"
