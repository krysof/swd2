#!/usr/bin/env python3
"""Lock same-turn attack-buff expiry after a resisted learned ability."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "pose_zero", "pose_four", "darkening_step_two",
    "darkening_step_three", "darkening_step_four", "darkest_pose_four",
    "sp332_frame0", "sp332_frame1", "sp332_frame2", "sp332_frame3",
    "sp332_frame4", "sp332_frame5", "sp332_frame6", "sp332_frame7",
    "resistance_card", "restoration_step_two", "restoration_step_three",
    "restoration_step_four", "restored_pose_four", "attack_buff_expired",
    "expiry_tail_clean", "monster_attack_card", "monster_tail_clean",
    "next_command",
)
EXPECTED_REWRITE_FRAMES = (
    121, 122, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
    134, 135, 136, 139, 140, 141, 142, 143, 144, 145, 166, 167,
)
EXPECTED_ORIGINAL_FRAMES = (
    3904, 3916, 3926, 3930, 3934, 3938, 3973, 3974, 3977, 3981,
    3986, 3990, 3994, 3998, 4001, 4021, 4023, 4025, 4027, 4062,
    4134, 4157, 4245, 4266,
)
EXPECTED_STABLE_THROUGH = (
    3915, 3924, 3928, 3932, 3936, 3972, 3973, 3976, 3980, 3985,
    3989, 3993, 3997, 3999, 4019, 4021, 4023, 4025, 4061, 4133,
    4156, 4174, 4265, 5250,
)
EXPECTED_TRANSITION_KINDS = (
    "first_darkening_transition", "first_restoration_transition",
)
EXPECTED_TRANSITION_FRAMES = (123, 138)
EXPECTED_TRANSITION_ORIGINAL_FRAMES = (3925, 4020)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed resisted-ability-expiry digest {label}")
    return value


def image_bytes(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid MZ header")
    return executable[header:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = expected.get("matched_frames")
        transitions = expected.get("unclaimed_transition_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_resisted_ability" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("finishing_ability_id"),
                 expected.get("finishing_effect_code")) != (6, 0x5e) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in pages) != \
                    EXPECTED_STABLE_THROUGH or \
                not isinstance(transitions, list) or \
                tuple(page.get("kind") for page in transitions) != \
                    EXPECTED_TRANSITION_KINDS or \
                tuple(page.get("rewrite_frame") for page in transitions) != \
                    EXPECTED_TRANSITION_FRAMES or \
                tuple(page.get("nearest_original_review_frame")
                      for page in transitions) != \
                    EXPECTED_TRANSITION_ORIGINAL_FRAMES:
            raise ValueError("unsupported FIG resisted-ability-expiry reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from resisted-ability-expiry reference")
        image = image_bytes(original_executable)
        instruction_checks = (
            ("learned_command_dispatch_image_offset",
             "learned_command_dispatch_machine_code", 0x0a97,
             "83bf232f047506e83838e99d01"),
            ("learned_action_image_offset", "learned_action_machine_code",
             0x42d9,
             "83bf843100741ce8d5eae894d0c7061f350400be012ee846e0e8e12bb81200e842f5c38b872b2fa397318b87332fa38d318b873b2fa38f318b87432fa29131882692318b874b2fa393318b87532fa395318b368f318a440da8207403e8df18e87deae83cd0e8952bb80300e8f6f48306e13104e869eae828d0e8812be8bdf8b80300e8dff48b368f318b740e83fe30770a56b80100e8c6175eeb03e85700d1e68b84bd2bc706a93c0200ffd0c706a93c0100e8d014e88600"),
            ("resistance_handler_image_offset",
             "resistance_handler_machine_code", 0x59a1,
             "89362d43a1e531b91400f7e1bfcc1d03f88a550c80e207be5b2f80fa017413be6a2fb8818180fa02740880fa057403be792fe8cdba8b1e973183bf6134007432e84ae2e8ef14891e3831c7061f350200e887c9be7b2de88a18813635430040b80b00e83101b80500e831dee81fe2e8c414c3"),
            ("player_expiry_dispatch_image_offset",
             "player_expiry_dispatch_machine_code", 0x0c41,
             "8b1e3a31d1e383bf3c3100741bff8f3c3183bf3c310075108b36dd318b445f89445dbe372de83e01"),
            ("expiry_card_image_offset", "expiry_card_machine_code", 0x0da7,
             "53c7061f35040056e80620e8c505a125432d08003d64007203b80000a31b35c7061d357800e87b2ac70627438100a11b35050200a325435ee8a264e8f160b81200e8522a5b"),
            ("cleanup_tail_image_offset", "cleanup_tail_machine_code", 0x0d98,
             "e81d20e83861b80500e8992ae968fb"),
        )
        for offset_key, code_key, wanted_offset, wanted_code in instruction_checks:
            offset = expected.get(offset_key)
            code = expected.get(code_key)
            if offset != wanted_offset or code != wanted_code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    f"FIG resisted-ability-expiry instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG resisted-ability-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG resisted-ability-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 5 or \
                expected.get("capture_time_limit_seconds") != 75 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 5250 or \
                expected.get("capture_video_frames") != 5256 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG resisted-ability-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Twenty-four stable pages" not in limitation or \
                "123 or 138" not in limitation or \
                "not claimed" not in limitation or \
                not all(token in limitation for token in (
                    "0a97", "42d9", "59a1", "0c41", "0da7", "0d98")):
            raise ValueError("FIG resisted-ability-expiry limitation is missing")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.game), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG resisted-ability-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG resisted-ability-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG resisted-ability-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        timed_frames = (
            (121, "pose_zero_at_milliseconds"),
            (122, "pose_four_at_milliseconds"),
            (123, "first_dark_at_milliseconds"),
            (136, "resistance_card_at_milliseconds"),
            (137, "handler_clean_at_milliseconds"),
            (142, "restored_pose_at_milliseconds"),
            (143, "expiry_at_milliseconds"),
            (144, "clean_at_milliseconds"),
            (145, "monster_attack_at_milliseconds"),
            (166, "monster_tail_clean_at_milliseconds"),
            (167, "next_command_at_milliseconds"),
        )
        if any(frame_times[index] != expected[key]
               for index, key in timed_frames) or \
                frame_times[122] - frame_times[121] != 165 or \
                frame_times[123] - frame_times[122] != 165 or \
                frame_times[137] - frame_times[136] != 439 or \
                expected.get("resistance_card_hold_milliseconds") != 165 or \
                frame_times[142] - frame_times[137] != 220 or \
                frame_times[143] - frame_times[142] != 55 or \
                frame_times[144] - frame_times[143] != 989 or \
                expected.get("expiry_hold_milliseconds") != 989 or \
                frame_times[145] - frame_times[144] != 274 or \
                frame_times[167] - frame_times[166] != 110:
            raise ValueError("FIG resisted-ability-expiry event timing differs")
        voices = [item for item in trace.get("timeline", [])
                  if item.get("kind") == "voice"]
        if len(voices) != 9 or \
                expected.get("finishing_voice_call") != 7 or \
                voices[7].get("at_milliseconds") != \
                    expected["finishing_voice_at_milliseconds"] or \
                voices[7].get("payload_fnv1a64") != \
                    expected["finishing_voice_fnv1a64"] or \
                expected.get("monster_voice_call") != 8 or \
                voices[8].get("at_milliseconds") != \
                    expected["monster_voice_at_milliseconds"] or \
                voices[8].get("payload_fnv1a64") != \
                    expected["monster_voice_fnv1a64"]:
            raise ValueError("FIG resisted-ability-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG resisted-ability-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG resisted-ability-expiry exact page differs: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        for page in transitions:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    page["rewrite_rgb_sha256"] == \
                        page["nearest_original_rgb_sha256"] or \
                    "original_rgb_sha256" in page:
                raise ValueError(
                    "FIG resisted-ability-expiry unclaimed transition differs: " +
                    page["kind"])
            digest(page.get("nearest_original_png_sha256"),
                   page["kind"] + "/nearest_original_png_sha256")
            digest(page.get("nearest_original_rgb_sha256"),
                   page["kind"] + "/nearest_original_rgb_sha256")

        # 0c41 enters 0da7 immediately after 4417 restores the learned pose.
        # The exact 80x32 target card footprint and the lack of a five-tick
        # bright bare page between frames 142 and 143 distinguish this path
        # from the no-expiry 0d98 boundary.
        restored_pixels = frames[142][0]
        expiry_pixels = frames[143][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(restored_pixels, expiry_pixels))
            if left != right
        ]
        bbox = (
            min(x for x, _ in changed), min(y for _, y in changed),
            max(x for x, _ in changed), max(y for _, y in changed))
        if len(changed) != expected["changed_from_restored_pose_pixels"] or \
                list(bbox) != expected["changed_from_restored_pose_bbox"] or \
                len(changed) != 2560 or bbox != (116, 120, 195, 151) or \
                frames[137] != frames[127] or \
                frames[142] != frames[122] or \
                frames[144] != frames[166] or \
                frames[142] == frames[143] or frames[143] == frames[144]:
            raise ValueError(
                "FIG resisted-ability expiry lost the retained learned pose")

        print(
            "FIG resisted-ability expiry checkpoint: ability 6 restores its "
            "learned pose, enters same-turn attack-buff expiry before the sole "
            "0d98 clean boundary, and matches 24 original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG resisted-ability expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
