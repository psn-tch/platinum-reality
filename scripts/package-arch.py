#!/usr/bin/env python3
"""Create a pinned local source snapshot and Arch recipe; never install anything."""
from datetime import datetime, timezone
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile

project = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, help="New output directory")
options = parser.parse_args()
destination = options.output or project / "build/packages" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
destination.mkdir(parents=True, exist_ok=False)
# The release version (docs/software-update.md): update 41 is 0.41.
version = (project / "VERSION").read_text().strip()
if not version or not all(part.isdigit() for part in version.split(".")):
    raise SystemExit(f"VERSION must be digits and dots, not {version!r}")
inputs = [project / n for n in ("VERSION", "CMakeLists.txt", "LICENSE", "README.md", "docs/install.md", "docs/first-session.md", "docs/x11-integration.md")]
for directory in ("src", "tests", "scripts", "packaging"):
    inputs.extend(p for p in sorted((project / directory).rglob("*"))
                  if p.is_file() and not p.is_symlink() and "__pycache__" not in p.parts)
record = {}
archive = destination / f"platinum-reality-{version}.tar.gz"
with archive.open("xb") as output:
    with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as tar:
            for path in sorted(inputs):
                relative = str(path.relative_to(project))
                contents = path.read_bytes()
                record[relative] = hashlib.sha256(contents).hexdigest()
                item = tarfile.TarInfo(f"platinum-reality-{version}/" + relative)
                item.size = len(contents)
                item.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
                tar.addfile(item, io.BytesIO(contents))
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
template = (project / "packaging/arch/PKGBUILD.in").read_text()
(destination / "PKGBUILD").write_text(template.replace("@SOURCE_SHA256@", digest).replace("@VERSION@", version))
(destination / "source-manifest.json").write_text(json.dumps({
    "archive_sha256": digest, "inputs_sha256": record,
    "scope": "Source snapshot: no personal assets, build outputs or profiles."
}, indent=2) + "\n")
# Keep makepkg output inside this disposable directory, regardless of user defaults.
(destination / "makepkg.conf").write_text(
    'source /etc/makepkg.conf\nPKGDEST="$PWD"\nSRCDEST="$PWD"\n'
    'SRCPKGDEST="$PWD"\nLOGDEST="$PWD"\nBUILDDIR="$PWD"\n')
print(destination)
