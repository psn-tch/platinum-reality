#!/usr/bin/env python3
"""Asynchronous desktop Sound-panel backend.

Status is read-only; changes require an explicit CLI/UI action. Tests inject the
command runner or an owned virtual audio server. No shell or elevation.
"""
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import time

PACTL = "/usr/bin/pactl"
MAX_DEVICES = 128
MAX_STREAMS = 512
MAX_REPLY = 2 * 1024 * 1024
USER_RUNTIME = Path("/run/user") / str(os.getuid())


def user_audio_server(runtime=None):
    # The desktop's XDG runtime is private and has no audio sockets. Address only the user's
    # own existing server, as system_controls.py does for wpctl.
    runtime = Path(runtime or USER_RUNTIME)
    server = runtime / "pulse/native"
    try:
        if (runtime.is_dir() and not runtime.is_symlink() and runtime.stat().st_uid == os.getuid()
                and server.is_socket()):
            return "unix:" + str(server)
    except OSError:
        pass
    return None


def run(arguments):
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    # The login session names the server; a desktop run inside another one may not.
    if not environment.get("PULSE_SERVER") and (server := user_audio_server()):
        environment["PULSE_SERVER"] = server
    child = subprocess.Popen([PACTL, *arguments], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, env=environment, bufsize=0)
    deadline = time.monotonic() + 5
    output = bytearray()
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(child.args, 5)
                for key, _ in selector.select(remaining):
                    chunk = os.read(key.fd, min(65536, MAX_REPLY + 1 - len(output)))
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    output.extend(chunk)
                    if len(output) > MAX_REPLY:
                        raise RuntimeError("The sound service returned too much information.")
        child.wait(timeout=max(0, deadline - time.monotonic()))
        if child.returncode:
            raise RuntimeError("The sound service could not complete the request.")
        return output.decode("utf-8", errors="replace").strip()
    finally:
        if child.poll() is None:
            child.kill()
        child.wait()
        child.stdout.close()


def label(value, fallback):
    if not isinstance(value, str):
        return fallback
    text = " ".join("".join(c if c.isprintable() else " " for c in value).split())[:120]
    return text or fallback


def name(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_.:+-]{1,512}", value):
        raise ValueError("The selected sound device is invalid.")
    if value.startswith("-"):
        raise ValueError("The selected sound device is invalid.")
    return value


def index(value):
    if type(value) is not int or not 0 <= value < 4294967295:
        raise ValueError("The sound service reported an invalid device number.")
    return value


class AudioControls:
    def __init__(self, runner=run, settle_timeout=2):
        self.run = runner
        self.settle_timeout = settle_timeout

    def settled(self, predicate, message):
        # Pulse acknowledges the request before a session manager has necessarily
        # applied its default/profile policy. Wait for actual state, without
        # issuing the mutation twice or claiming success from its exit code alone.
        deadline = time.monotonic() + self.settle_timeout
        while True:
            state = self.snapshot()
            if predicate(state):
                return state
            if time.monotonic() >= deadline:
                raise RuntimeError(message)
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))

    def rows(self, category, limit=MAX_DEVICES):
        text = self.run(["--format=json", "list", category])
        if len(text) > MAX_REPLY:
            raise RuntimeError("The sound service returned too much information.")
        try:
            rows = json.loads(text)
        except (ValueError, TypeError) as error:
            raise RuntimeError("The sound device list could not be read.") from error
        if not isinstance(rows, list) or len(rows) > limit or not all(isinstance(row, dict) for row in rows):
            raise RuntimeError("The sound device list could not be read.")
        return rows

    def default(self, kind):
        try:
            return name(self.run(["get-default-" + kind]))
        except (RuntimeError, ValueError):
            return ""

    @staticmethod
    def endpoint(row):
        device_name = name(row.get("name"))
        device_index = index(row.get("index"))
        properties = row.get("properties", {})
        if not isinstance(properties, dict):
            properties = {}
        volume = row.get("volume", {})
        levels = []
        if isinstance(volume, dict):
            for channel in volume.values():
                if isinstance(channel, dict) and type(channel.get("value")) is int and channel["value"] >= 0:
                    levels.append(channel["value"])
        card = row.get("card")
        return {"name": device_name, "index": device_index,
                "description": label(row.get("description"), device_name),
                "card": card if type(card) is int and 0 <= card < 4294967295 else -1,
                "muted": row.get("mute") is True,
                "volume": min(100, round(sum(levels) / len(levels) * 100 / 65536)) if levels else -1,
                "bluetooth": properties.get("device.bus") == "bluetooth" or device_name.startswith("bluez_"),
                "monitor": device_name.endswith(".monitor") or properties.get("device.class") == "monitor"}

    @staticmethod
    def card(row):
        profiles = row.get("profiles", [])
        if isinstance(profiles, dict):
            profiles = [{**data, "name": key} for key, data in profiles.items() if isinstance(data, dict)]
        if not isinstance(profiles, list) or len(profiles) > 128:
            raise RuntimeError("The sound profile list could not be read.")
        choices = []
        for profile in profiles:
            if not isinstance(profile, dict):
                raise RuntimeError("The sound profile list could not be read.")
            profile_name = name(profile.get("name"))
            choices.append({"name": profile_name, "description": label(profile.get("description"), profile_name),
                            "available": profile.get("available") not in ("no", False)})
        properties = row.get("properties", {})
        if not isinstance(properties, dict):
            properties = {}
        active = row.get("active_profile", "")
        if isinstance(active, dict):
            active = active.get("name", "")
        return {"name": name(row.get("name")), "index": index(row.get("index")),
                "description": label(properties.get("device.description"), row["name"]),
                "activeProfile": active if isinstance(active, str) else "", "profiles": choices}

    def snapshot(self):
        try:
            outputs = [self.endpoint(row) for row in self.rows("sinks")]
            inputs = [self.endpoint(row) for row in self.rows("sources")]
            cards = [self.card(row) for row in self.rows("cards")]
        except (ValueError, KeyError) as error:
            raise RuntimeError("The sound device list could not be read.") from error
        for collection in (outputs, inputs, cards):
            if len({row["name"] for row in collection}) != len(collection) or len({row["index"] for row in collection}) != len(collection):
                raise RuntimeError("The sound service reported ambiguous devices.")
        return {"outputs": sorted(outputs, key=lambda row: row["description"].casefold()),
                "inputs": sorted((row for row in inputs if not row["monitor"]), key=lambda row: row["description"].casefold()),
                "cards": cards, "defaultOutput": self.default("sink"), "defaultInput": self.default("source")}

    def select(self, kind, target):
        if kind not in ("output", "input"):
            raise ValueError("Choose a sound output or input.")
        target = name(target)
        state = self.snapshot()
        collection, default_key, endpoint, streams, field = (
            ("outputs", "defaultOutput", "sink", "sink-inputs", "sink") if kind == "output" else
            ("inputs", "defaultInput", "source", "source-outputs", "source"))
        selected = next((row for row in state[collection] if row["name"] == target), None)
        if selected is None:
            raise ValueError("That sound device is no longer available. Refresh and choose again.")
        if state[default_key] == target:
            return {"state": state, "warning": ""}
        previous = next((row for row in state[collection] if row["name"] == state[default_key]), None)
        # Read streams before changing anything. Move only streams on the previous
        # default; preserve applications explicitly routed to another device.
        playing = self.rows(streams, MAX_STREAMS) if previous is not None else []
        moves = []
        for stream in playing:
            if type(stream.get(field)) is int and stream[field] == previous["index"]:
                moves.append(index(stream.get("index")))
        self.run(["set-default-" + endpoint, target])
        failed = 0
        for stream in moves:
            try:
                self.run(["move-" + ("sink-input" if kind == "output" else "source-output"), str(stream), target])
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                failed += 1
        state = self.settled(lambda state: state[default_key] == target and
                             any(row["name"] == target for row in state[collection]),
                             "The sound device disappeared before the choice could be applied.")
        return {"state": state, "warning": "Some applications could not move to the selected device." if failed else ""}

    def profile(self, card_name, profile_name):
        card_name, profile_name = name(card_name), name(profile_name)
        state = self.snapshot()
        card = next((row for row in state["cards"] if row["name"] == card_name), None)
        profile = next((row for row in card["profiles"] if row["name"] == profile_name), None) if card else None
        if profile is None or not profile["available"]:
            raise ValueError("That sound profile is no longer available. Refresh and choose again.")
        if card["activeProfile"] != profile_name:
            self.run(["set-card-profile", card_name, profile_name])
        state = self.settled(lambda state: any(row["name"] == card_name and row["activeProfile"] == profile_name
                                              for row in state["cards"]),
                             "The sound profile could not be applied. Refresh and try again.")
        return {"state": state, "warning": ""}

    def level(self, kind, target, value, mute=False):
        if kind not in ("output", "input") or type(value) is not int or not 0 <= value <= (1 if mute else 100):
            raise ValueError("Invalid sound level.")
        target = name(target)
        state = self.snapshot()
        if not any(row["name"] == target for row in state["outputs" if kind == "output" else "inputs"]):
            raise ValueError("That sound device is no longer available. Refresh and choose again.")
        self.run(["set-" + ("sink" if kind == "output" else "source") + ("-mute" if mute else "-volume"),
                  target, str(value) if mute else str(value) + "%"])
        return {"state": self.snapshot(), "warning": ""}


def main(arguments):
    controls = AudioControls()
    if arguments == ["status"]:
        return {"state": controls.snapshot(), "warning": ""}
    if len(arguments) == 2 and arguments[0] in ("output", "input"):
        return controls.select(*arguments)
    if len(arguments) == 3 and arguments[0] == "profile":
        return controls.profile(*arguments[1:])
    if len(arguments) == 4 and arguments[0] in ("volume", "mute"):
        return controls.level(arguments[1], arguments[2], int(arguments[3]), mute=arguments[0] == "mute")
    raise ValueError("Invalid sound control request.")


if __name__ == "__main__":
    try:
        print(json.dumps(main(sys.argv[1:])))
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"error": str(error), "state": {}}))
        raise SystemExit(1)
