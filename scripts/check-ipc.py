#!/usr/bin/env python3
"""Fail clearly before GUI startup if this execution venue denies local IPC."""
import os
import socket
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="frankenstein-ipc-") as directory:
    path = os.path.join(directory, "probe")
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
            probe.bind(path)
            probe.listen(1)
    except OSError as error:
        print(f"Native session blocked: Unix socket bind/listen failed: {error}", file=sys.stderr)
        print("Use a normal user terminal with local IPC enabled. No root access or desktop replacement is required.", file=sys.stderr)
        sys.exit(77)
