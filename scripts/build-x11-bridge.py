#!/usr/bin/env python3
"""Build the pinned X11 bridge in a new user-local directory; no installation."""
import argparse
from datetime import datetime, timezone
import hashlib
import os
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = "0.8.3"
COMMIT = "b83eab900644e4c7c77982ce3d44cb490f0c5e1d"
URL = "https://codeload.github.com/Supreeeme/xwayland-satellite/tar.gz/" + COMMIT
ARCHIVE_SHA256 = "45f06d0c7017d793fa20c85a6fa7e8b26ad3c681c579d455f697faddf3c57bdf"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="New build directory; existing content is never overwritten")
    parser.add_argument("--patch", type=Path, help="Compatibility patch (default: adjacent or project packaging)")
    parser.add_argument("--archive", type=Path,
                        help="The pinned source archive, already downloaded (a package build's source=); "
                             "it must match the recorded hash")
    options = parser.parse_args()
    patch = options.patch or Path(__file__).with_name("xwayland-satellite-qt-shell-v1.patch")
    if not patch.is_file():
        patch = Path(__file__).resolve().parents[1] / "packaging/xwayland-satellite-qt-shell-v1.patch"
    if not patch.is_file():
        parser.error("Supply the recorded Qt compatibility patch using --patch")
    patch = patch.resolve()
    for command in ("cargo", "patch", "pkg-config", "clang"):
        if not shutil.which(command):
            parser.error("Required existing build tool is missing: " + command)
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    archive = output / "source.tar.gz"
    if options.archive:
        shutil.copyfile(options.archive, archive)
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Platinum-Dreams-local-build"})
        with urllib.request.urlopen(request, timeout=60) as response, archive.open("xb") as destination:
            shutil.copyfileobj(response, destination)
    if digest(archive) != ARCHIVE_SHA256:
        raise SystemExit("Source archive hash mismatch; nothing was extracted or executed")
    with tarfile.open(archive) as package:
        if any(not (member.isfile() or member.isdir()) for member in package.getmembers()):
            raise SystemExit("Source archive contains unexpected special files")
        package.extractall(output, filter="data")
    source = output / ("xwayland-satellite-" + COMMIT)
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=source, check=True)
    command = ["cargo", "build", "--release", "--locked", "--features", "fontconfig", "--jobs", "4"]
    # Paths inside the binary name the project, not the folder it happened to be built in.
    environment = dict(os.environ)
    environment["RUSTFLAGS"] = (environment.get("RUSTFLAGS", "") +
                                f" --remap-path-prefix={source}=/usr/src/xwayland-satellite").strip()
    subprocess.run(command, cwd=source, check=True, env=environment)
    binary = source / "target/release/xwayland-satellite"
    for name in ("LICENSE", "Cargo.lock"):
        shutil.copyfile(source / name, output / name)
    record = {"project": "xwayland-satellite", "version": VERSION, "commit": COMMIT,
              "url": URL, "sha256": ARCHIVE_SHA256,
              "retrieved_utc": datetime.now(timezone.utc).isoformat(),
              "source_directory": source.name, "build_command": command,
              "license": "MPL-2.0 (original LICENSE retained)",
              "font_policy": "fontconfig feature; no embedded font bytes",
              "cargo_lock_sha256": digest(output / "Cargo.lock"),
              "compatibility_patch_sha256": digest(patch),
              "patched_binary_sha256": digest(binary)}
    (output / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print("Built:", binary)
    print("CMake PLATINUM_XWAYLAND_PROVENANCE:", output)

if __name__ == "__main__":
    main()
