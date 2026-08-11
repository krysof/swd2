#!/usr/bin/env python3
"""Lock FIG player attack-buff expiry after a learned 58fa failure."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "retained_pose_zero", "retained_pose_four", "missing_medium_card",
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
    "monster_tail_clean", "next_command",
)
EXPECTED_REWRITE_FRAMES = (121, 122, 123, 124, 125, 126, 147, 148)
EXPECTED_ORIGINAL_FRAMES = (3908, 3920, 3999, 4031, 4101, 4123, 4211, 4232)
EXPECTED_STABLE_THROUGH = (3919, 3927, 4030, 4100, 4119, 4140, 4231, 4250)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed missing-medium-expiry digest {label}")
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
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_missing_medium" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                expected.get("buff_ability_id") != 38 or \
                expected.get("buff_effect_code") != 0x43 or \
                expected.get("finishing_ability_id") != 86 or \
                expected.get("finishing_effect_code") != 0x63 or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in pages) != \
                    EXPECTED_STABLE_THROUGH:
            raise ValueError("unsupported FIG missing-medium-expiry reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from missing-medium-expiry reference")
        image = image_bytes(original_executable)
        instruction_checks = (
            ("player_pose_sequence_image_offset",
             "player_pose_sequence_machine_code", 0x4338,
             "e87deae83cd0e8952bb80300e8f6f48306e13104e869eae828d0e8812be8bdf8b80300e8dff48b368f31"),
            ("missing_medium_handler_image_offset",
             "missing_medium_handler_machine_code", 0x58fa,
             "b90400bb000083bfa53150730639879d31744783c302e2ee813635430040c7061b351000c7061d354b00c7061f351200e81ddfc70625431200c70627435400be1f2de84519813635430040b80200e8ec01b80900e8ecde83c402c3"),
            ("expiry_wait_image_offset", "expiry_wait_machine_code",
             0x0de5, "b81200e8522a"),
            ("cleanup_tail_image_offset", "cleanup_tail_machine_code",
             0x0d98, "e81d20e83861b80500e8992ae968fb"),
        )
        for offset_key, code_key, wanted_offset, wanted_code in instruction_checks:
            offset = expected.get(offset_key)
            code = expected.get(code_key)
            if offset != wanted_offset or code != wanted_code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    f"FIG missing-medium-expiry instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG missing-medium-expiry fixture differs")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG missing-medium-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 5 or \
                expected.get("capture_time_limit_seconds") != 75 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 5250 or \
                expected.get("capture_video_frames") != 5256 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG missing-medium-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "post-DAC RGB" not in limitation or \
                "4031..4100" not in limitation or \
                "18 INT 08h ticks" not in limitation or \
                "58fa" not in limitation or "0c41" not in limitation:
            raise ValueError("FIG missing-medium-expiry limitation is missing")

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
            raise ValueError("FIG missing-medium-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG missing-medium-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG missing-medium-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        pose_zero, pose_four, missing, expiry, clean, monster_attack, \
            monster_clean, next_command = EXPECTED_REWRITE_FRAMES
        expected_times = tuple(
            expected[name] for name in (
                "pose_zero_at_milliseconds", "pose_four_at_milliseconds",
                "missing_medium_at_milliseconds", "expiry_at_milliseconds",
                "clean_at_milliseconds", "monster_attack_at_milliseconds",
                "monster_tail_clean_at_milliseconds",
                "next_command_at_milliseconds"))
        if tuple(frame_times[index] for index in EXPECTED_REWRITE_FRAMES) != \
                expected_times or \
                frame_times[pose_four] - frame_times[pose_zero] != 165 or \
                frame_times[missing] - frame_times[pose_four] != 165 or \
                frame_times[expiry] - frame_times[missing] != 494 or \
                frame_times[clean] - frame_times[expiry] != \
                    expected["expiry_hold_milliseconds"] or \
                expected["expiry_hold_milliseconds"] != 989 or \
                frame_times[monster_attack] - frame_times[clean] != 275 or \
                frame_times[next_command] - frame_times[monster_clean] != 110:
            raise ValueError("FIG missing-medium-expiry event timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG missing-medium-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG missing-medium-expiry page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        # The 0da7 page preserves 4338's pose-four fighter at the action
        # anchor. Its differences from the prior pose-four page are exactly
        # the compact 80x32 expiry card plus the already-paid AP glyph pixels.
        pose_pixels = frames[pose_four][0]
        expiry_pixels = frames[expiry][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(pose_pixels, expiry_pixels)) if left != right
        ]
        bbox = (
            min(x for x, _ in changed), min(y for _, y in changed),
            max(x for x, _ in changed), max(y for _, y in changed))
        if len(changed) != expected["changed_from_pose_four_pixels"] or \
                list(bbox) != expected["changed_from_pose_four_bbox"] or \
                len(changed) != 2568 or bbox != (12, 120, 91, 198):
            raise ValueError("FIG missing-medium expiry lost learned pose four")
        if frames[clean] != frames[monster_clean] or \
                frames[missing] == frames[expiry] or \
                frames[expiry] == frames[clean]:
            raise ValueError("FIG missing-medium-expiry cleanup ordering differs")

        print(
            "FIG missing-medium-expiry checkpoint: learned 58fa failure "
            "retains pose four for attack-buff expiry, holds 18 ticks, cleans "
            "once, and matches eight original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG missing-medium-expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
