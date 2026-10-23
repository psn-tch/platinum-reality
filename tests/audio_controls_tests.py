import copy
import json
import os
from pathlib import Path
import socket
import tempfile
import time
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/desktop"))
from unittest.mock import patch
from audio_controls import AudioControls, MAX_DEVICES, MAX_REPLY, run as run_audio_command


def endpoint(number, name, description, card=-1):
    return {"index": number, "name": name, "description": description, "card": card, "mute": False,
            "volume": {"front-left": {"value": 32768}, "front-right": {"value": 32768}}}


class FakeAudio:
    def __init__(self):
        self.outputs = [endpoint(1, "speakers", "Internal Speakers"),
                        endpoint(2, "bluez_output.headset", "Bluetooth Headset", 8),
                        endpoint(3, "usb_output", "USB Speakers")]
        self.inputs = [endpoint(4, "microphone", "Internal Microphone"),
                       endpoint(5, "bluez_input.headset", "Headset Microphone", 8),
                       endpoint(6, "speakers.monitor", "Monitor of Internal Speakers")]
        self.cards = [{"index": 8, "name": "bluez_card.headset", "properties": {"device.description": "Headset"},
                       "active_profile": "a2dp-sink", "profiles": [
                           {"name": "a2dp-sink", "description": "High Fidelity Playback", "available": "yes"},
                           {"name": "headset-head-unit", "description": "Headset with Microphone", "available": "yes"},
                           {"name": "unavailable", "description": "Unavailable", "available": "no"}]}]
        self.default_output, self.default_input = "speakers", "microphone"
        self.streams = [{"index": 10, "sink": 1}, {"index": 11, "sink": 3}, {"index": 12, "sink": 1}]
        self.recordings = [{"index": 20, "source": 4}, {"index": 21, "source": 6}]
        self.calls = []
        self.fail = set()
        self.disappear = False
        self.ignore_profile = False

    def __call__(self, arguments):
        self.calls.append(arguments)
        if tuple(arguments) in self.fail:
            raise RuntimeError("Fixture failure")
        if arguments[:2] == ["--format=json", "list"]:
            return json.dumps({"sinks": self.outputs, "sources": self.inputs, "cards": self.cards,
                               "sink-inputs": self.streams, "source-outputs": self.recordings}[arguments[2]])
        if arguments == ["get-default-sink"]:
            return self.default_output
        if arguments == ["get-default-source"]:
            return self.default_input
        if arguments[0] == "set-default-sink":
            self.default_output = arguments[1]
            if self.disappear:
                self.outputs = [row for row in self.outputs if row["name"] != arguments[1]]
        elif arguments[0] == "set-default-source":
            self.default_input = arguments[1]
        elif arguments[0] == "set-card-profile":
            if not self.ignore_profile:
                next(row for row in self.cards if row["name"] == arguments[1])["active_profile"] = arguments[2]
        elif arguments[0].endswith("-volume"):
            rows = self.outputs if arguments[0] == "set-sink-volume" else self.inputs
            selected = next(row for row in rows if row["name"] == arguments[1])
            level = round(int(arguments[2].removesuffix("%")) * 65536 / 100)
            selected["volume"] = {"mono": {"value": level}}
        elif arguments[0].endswith("-mute"):
            rows = self.outputs if arguments[0] == "set-sink-mute" else self.inputs
            next(row for row in rows if row["name"] == arguments[1])["mute"] = arguments[2] == "1"
        elif not arguments[0].startswith("move-"):
            raise AssertionError(arguments)
        return ""

    def mutations(self):
        return [call for call in self.calls if call[0].startswith(("set-", "move-"))]


class AudioProcessTests(unittest.TestCase):
    def program(self, body):
        temporary = tempfile.TemporaryDirectory(prefix="platinum-audio-command-")
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "fake pactl"
        path.write_text("#!/usr/bin/python3\n" + body)
        path.chmod(0o700)
        return path

    def test_exact_arguments_and_explicit_audio_server_are_preserved(self):
        program = self.program("import json,os,sys\nprint(json.dumps({'args':sys.argv[1:],"
                               "'server':os.environ.get('PULSE_SERVER'),'locale':os.environ.get('LC_ALL')}))\n")
        with patch("audio_controls.PACTL", str(program)), patch.dict(os.environ, {"PULSE_SERVER": "unix:/fixture/audio.sock"}):
            result = json.loads(run_audio_command(["set-default-sink", "bluez_output.headset"]))
        self.assertEqual(result, {"args": ["set-default-sink", "bluez_output.headset"],
                                  "server": "unix:/fixture/audio.sock", "locale": "C"})

    def test_without_a_named_server_only_the_users_own_audio_socket_is_used(self):
        # A desktop running inside another session has a private XDG runtime: pactl would look
        # there and find nothing.
        program = self.program("import json,os\nprint(json.dumps(os.environ.get('PULSE_SERVER')))\n")
        temporary = tempfile.TemporaryDirectory(prefix="audio-runtime-")
        self.addCleanup(temporary.cleanup)
        runtime = Path(temporary.name)
        environment = {k: v for k, v in os.environ.items() if k != "PULSE_SERVER"}
        with patch("audio_controls.PACTL", str(program)), patch("audio_controls.USER_RUNTIME", runtime), \
                patch.dict(os.environ, environment, clear=True):
            self.assertIsNone(json.loads(run_audio_command(["info"])))
            (runtime / "pulse").mkdir()
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(runtime / "pulse/native"))
                self.assertEqual(json.loads(run_audio_command(["info"])), "unix:" + str(runtime / "pulse/native"))
                # A server the session named is never replaced.
                with patch.dict(os.environ, {"PULSE_SERVER": "unix:/fixture/audio.sock"}):
                    self.assertEqual(json.loads(run_audio_command(["info"])), "unix:/fixture/audio.sock")

    def test_oversized_output_is_stopped_while_the_command_is_still_running(self):
        program = self.program("import os,time\nfor _ in range(768): os.write(1,b'x'*4096)\ntime.sleep(60)\n")
        started = time.monotonic()
        with patch("audio_controls.PACTL", str(program)), self.assertRaisesRegex(RuntimeError, "too much"):
            run_audio_command(["status"])
        self.assertLess(time.monotonic() - started, 2)

    def test_large_stderr_does_not_block_the_bounded_stdout_reply(self):
        program = self.program("import os\nfor _ in range(768): os.write(2,b'x'*4096)\nprint('ok')\n")
        with patch("audio_controls.PACTL", str(program)):
            self.assertEqual(run_audio_command(["status"]), "ok")


class AudioControlsTests(unittest.TestCase):
    def setUp(self):
        self.fake = FakeAudio()
        self.audio = AudioControls(self.fake, settle_timeout=0)

    def test_a_default_request_waits_for_policy_without_reissuing_the_mutation(self):
        pending = []
        original = self.fake.__call__
        reads = 0
        def delayed(arguments):
            nonlocal reads
            if arguments[0] == "set-default-sink":
                self.fake.calls.append(arguments)
                pending.append(arguments[1])
                return ""
            if pending and arguments == ["get-default-sink"]:
                reads += 1
                if reads == 3:
                    self.fake.default_output = pending[0]
            return original(arguments)
        result = AudioControls(delayed, settle_timeout=0.5).select("output", "bluez_output.headset")
        self.assertEqual(result["state"]["defaultOutput"], "bluez_output.headset")
        self.assertEqual(sum(call[0] == "set-default-sink" for call in self.fake.calls), 1)
        self.assertEqual(reads, 3)

    def test_a_profile_request_waits_for_policy_without_reissuing_the_mutation(self):
        pending = []
        original = self.fake.__call__
        reads = 0
        def delayed(arguments):
            nonlocal reads
            if arguments[0] == "set-card-profile":
                self.fake.calls.append(arguments)
                pending.append(arguments[2])
                return ""
            if pending and arguments == ["--format=json", "list", "cards"]:
                reads += 1
                if reads == 3:
                    self.fake.cards[0]["active_profile"] = pending[0]
            return original(arguments)
        result = AudioControls(delayed, settle_timeout=0.5).profile("bluez_card.headset", "headset-head-unit")
        self.assertEqual(result["state"]["cards"][0]["activeProfile"], "headset-head-unit")
        self.assertEqual(sum(call[0] == "set-card-profile" for call in self.fake.calls), 1)
        self.assertEqual(reads, 3)

    def test_status_is_read_only_and_lists_real_inputs_instead_of_monitors(self):
        state = self.audio.snapshot()
        self.assertEqual(state["defaultOutput"], "speakers")
        self.assertEqual(state["defaultInput"], "microphone")
        self.assertEqual(len(state["outputs"]), 3)
        self.assertEqual({row["name"] for row in state["inputs"]}, {"microphone", "bluez_input.headset"})
        self.assertEqual(state["outputs"][0]["name"], "bluez_output.headset")
        self.assertTrue(state["outputs"][0]["bluetooth"])
        self.assertEqual(state["outputs"][0]["volume"], 50)
        self.assertFalse(self.fake.mutations())

    def test_output_choice_moves_only_streams_on_the_previous_default(self):
        result = self.audio.select("output", "bluez_output.headset")
        self.assertEqual(result["state"]["defaultOutput"], "bluez_output.headset")
        self.assertEqual(self.fake.mutations(), [["set-default-sink", "bluez_output.headset"],
            ["move-sink-input", "10", "bluez_output.headset"], ["move-sink-input", "12", "bluez_output.headset"]])
        self.assertEqual(result["warning"], "")

    def test_microphone_choice_moves_only_recordings_on_the_previous_default(self):
        result = self.audio.select("input", "bluez_input.headset")
        self.assertEqual(result["state"]["defaultInput"], "bluez_input.headset")
        self.assertEqual(self.fake.mutations(), [["set-default-source", "bluez_input.headset"],
                                               ["move-source-output", "20", "bluez_input.headset"]])

    def test_reselecting_the_current_device_does_not_change_or_move_anything(self):
        self.audio.select("output", "speakers")
        self.assertFalse(self.fake.mutations())

    def test_partial_stream_failure_keeps_the_device_choice_and_reports_it(self):
        self.fake.fail.add(("move-sink-input", "10", "bluez_output.headset"))
        result = self.audio.select("output", "bluez_output.headset")
        self.assertTrue(result["warning"])
        self.assertEqual(result["state"]["defaultOutput"], "bluez_output.headset")
        self.assertIn(["move-sink-input", "12", "bluez_output.headset"], self.fake.calls)

    def test_unreadable_streams_prevent_a_partial_default_change(self):
        self.fake.fail.add(("--format=json", "list", "sink-inputs"))
        with self.assertRaises(RuntimeError):
            self.audio.select("output", "bluez_output.headset")
        self.assertFalse(self.fake.mutations())

    def test_missing_previous_default_changes_new_stream_routing_without_guessing_moves(self):
        self.fake.default_output = "gone"
        self.audio.select("output", "bluez_output.headset")
        self.assertEqual(self.fake.mutations(), [["set-default-sink", "bluez_output.headset"]])

    def test_device_disappearing_during_selection_is_reported(self):
        self.fake.disappear = True
        with self.assertRaisesRegex(RuntimeError, "disappeared"):
            self.audio.select("output", "bluez_output.headset")

    def test_profile_choice_uses_only_available_advertised_profiles(self):
        result = self.audio.profile("bluez_card.headset", "headset-head-unit")
        self.assertEqual(result["state"]["cards"][0]["activeProfile"], "headset-head-unit")
        self.assertEqual(self.fake.mutations(), [["set-card-profile", "bluez_card.headset", "headset-head-unit"]])
        self.fake.calls.clear()
        for profile in ("unavailable", "invented"):
            with self.assertRaises(ValueError):
                self.audio.profile("bluez_card.headset", profile)
        self.assertFalse(self.fake.mutations())

    def test_failed_profile_acknowledgement_is_not_reported_as_success(self):
        self.fake.ignore_profile = True
        with self.assertRaisesRegex(RuntimeError, "could not be applied"):
            self.audio.profile("bluez_card.headset", "headset-head-unit")

    def test_profile_dictionary_and_active_profile_objects_are_supported(self):
        card = self.fake.cards[0]
        card["profiles"] = {row["name"]: row for row in card["profiles"]}
        card["active_profile"] = {"name": "a2dp-sink"}
        parsed = self.audio.snapshot()["cards"][0]
        self.assertEqual(parsed["activeProfile"], "a2dp-sink")
        self.assertEqual(len(parsed["profiles"]), 3)

    def test_levels_and_mute_target_exact_devices_and_never_exceed_100_percent(self):
        state = self.audio.level("output", "bluez_output.headset", 68)["state"]
        self.assertEqual(next(row for row in state["outputs"] if row["name"] == "bluez_output.headset")["volume"], 68)
        self.audio.level("input", "microphone", 1, mute=True)
        self.assertEqual(self.fake.mutations(), [["set-sink-volume", "bluez_output.headset", "68%"],
                                               ["set-source-mute", "microphone", "1"]])
        for value in (-1, 101, True, "50;id"):
            with self.assertRaises(ValueError):
                self.audio.level("output", "speakers", value)

    def test_invalid_or_removed_devices_never_reach_a_mutation(self):
        for target in ("--help", "@DEFAULT_SINK@", "speakers;id", "missing", "a" * 513):
            with self.subTest(target=target), self.assertRaises(ValueError):
                self.audio.select("output", target)
        with self.assertRaises(ValueError):
            self.audio.select("shell", "speakers")
        with self.assertRaises(ValueError):
            self.audio.select("input", "speakers.monitor")
        self.assertFalse(self.fake.mutations())

    def test_duplicate_device_names_or_numbers_are_not_accepted(self):
        for field in ("name", "index"):
            with self.subTest(field=field):
                fake = FakeAudio()
                fake.outputs[1][field] = fake.outputs[0][field]
                with self.assertRaisesRegex(RuntimeError, "ambiguous"):
                    AudioControls(fake).snapshot()

    def test_malformed_or_unbounded_device_replies_are_rejected(self):
        for response in ("not json", "{}", "[4]", " " * (MAX_REPLY + 1), json.dumps([{}] * (MAX_DEVICES + 1))):
            with self.subTest(response=response[:40]), self.assertRaises(RuntimeError):
                AudioControls(lambda _: response).snapshot()
        for value in (True, -1, 4294967295, "4"):
            fake = FakeAudio()
            fake.outputs[0]["index"] = value
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                AudioControls(fake).snapshot()

    def test_descriptions_are_plain_bounded_text_and_missing_volume_stays_unavailable(self):
        self.fake.outputs[0]["description"] = "Speaker\n" + "x" * 200
        self.fake.outputs[0]["volume"] = {}
        output = next(row for row in self.audio.snapshot()["outputs"] if row["name"] == "speakers")
        self.assertNotIn("\n", output["description"])
        self.assertLessEqual(len(output["description"]), 120)
        self.assertEqual(output["volume"], -1)


if __name__ == "__main__":
    unittest.main()
