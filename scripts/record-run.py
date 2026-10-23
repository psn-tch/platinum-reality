#!/usr/bin/env python3
"""Record bounded build/source provenance without collecting unrelated user data."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import sys

project, destination = (Path(argument).resolve() for argument in sys.argv[1:3])
build = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else project / "build"
if not build.is_relative_to(project):
    raise SystemExit("Native evidence build directory must be inside this project")
inputs = [project / "CMakeLists.txt"]
# Everything under src/ (drawings such as the title-bar SVGs are compiled in); code files elsewhere.
inputs.extend(path for path in sorted((project / "src").rglob("*")) if path.is_file() and not path.is_symlink())
for directory in ("tests", "scripts"):
    inputs.extend(path for path in sorted((project / directory).rglob("*"))
                  if path.suffix in (".cpp", ".hpp", ".c", ".qml", ".css", ".sh", ".py", ".json", ".xml") and path.is_file())
inputs.extend(path for path in sorted((project / "packaging").rglob("*"))
              if path.is_file() and not path.is_symlink())
inputs.extend(build / name for name in ("frankenstein", "platinum-wallet-service", "wallet-activation-tests", "native-tests", "multi-display-tests", "audio-service-tests", "sound-integration-tests", "display-layout-tests", "display-topology-feed-tests", "native-probe", "vulkan-probe", "frankenstein-finder", "frankenstein-editor", "frankenstein-drop-receiver", "frankenstein-puzzle", "gtk-appearance-probe3", "gtk-appearance-probe4",
                                                  "pointer-probe", "paste-probe", "virtual-pointer-driver", "relative-mouse-probe", "output-race-probe",
                                                  "platinum-lock", "lock-tests", "virtual-keyboard-driver"))
inputs.append(build / "plugins/styles/libfrankenstein-style.so")
inputs.extend(build / name for name in ("xwayland/Xwayland", "xwayland/libplatinum-xwayland-implicit-sync.so"))
inputs.append(build / "CMakeCache.txt")
for name in ("xwayland-satellite", "xwayland-satellite-provenance.json"):
    inputs.append(build / name)
bridge_provenance = build / "xwayland-satellite-provenance.json"
if bridge_provenance.exists():
    bridge = build / "xwayland-satellite"
    dependency = json.loads(bridge_provenance.read_text())
    if (not bridge.is_file() or hashlib.sha256(bridge.read_bytes()).hexdigest() !=
            dependency.get("patched_binary_sha256")):
        raise SystemExit("X11 bridge does not match its recorded build provenance")
    patch = project / "packaging/xwayland-satellite-qt-shell-v1.patch"
    if hashlib.sha256(patch.read_bytes()).hexdigest() != dependency.get("compatibility_patch_sha256"):
        raise SystemExit("X11 compatibility patch changed: rebuild and record the bridge first")
external_style = os.environ.get("FRANKENSTEIN_EXTERNAL_STYLE_DIRECTORY")
if external_style:
    style = (Path(external_style) / "styles/libfrankenstein-style.so").resolve()
    if not style.is_relative_to(project) or not style.is_file():
        raise SystemExit("External fixture style must be a recorded build artifact inside this project")
    inputs.append(style)
record = {
    "recorded_before_run": True,
    "time_utc": datetime.now(timezone.utc).isoformat(),
    "machine": platform.machine(),
    "native_build_directory": str(build.relative_to(project)),
    "render_environment": {key: os.environ[key] for key in (
        "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_SCALE_FACTOR_ROUNDING_POLICY",
        "QT_QUICK_BACKEND", "QSG_RHI_BACKEND", "QT_QPA_PLATFORM",
        "QTEST_FUNCTION_TIMEOUT", "ASAN_OPTIONS", "UBSAN_OPTIONS",
        "QT_WAYLAND_HARDWARE_INTEGRATION", "FRANKENSTEIN_EXTERNAL_STYLE_DIRECTORY",
        "FRANKENSTEIN_MULTI_DISPLAY_EXPERIMENT") if key in os.environ},
    "sha256": {str(path.relative_to(project)): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in inputs if path.exists()},
}
destination.mkdir(parents=True, exist_ok=True)
(destination / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
