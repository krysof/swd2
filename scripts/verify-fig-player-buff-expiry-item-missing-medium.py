#!/usr/bin/env python3
"""Lock FIG attack-buff expiry after direct item 226 lacks its medium."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXACT_KINDS = (
    "item_pose_zero", "darkest_item_pose_zero", "dark_missing_medium_card",
    "expiry_tail_clean", "monster_tail_clean", "next_command",
)
EXACT_REWRITE_FRAMES = (121, 126, 127, 134, 156, 157)
EXACT_ORIGINAL_FRAMES = (3900, 3922, 3952, 4082, 4192, 4213)
EXACT_STABLE_THROUGH = (3908, 3949, 3968, 4100, 4212, 4250)
CROP_KINDS = (
    "restored_missing_medium_card", "attack_buff_expired",
    "monster_attack_card",
)
CROP_REWRITE_FRAMES = (132, 133, 135)
CROP_ORIGINAL_FRAMES = (3981, 4012, 4105)
CROP_STABLE_THROUGH = (4011, 4081, 4122)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item-missing-medium digest {label}")
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
        exact = expected.get("matched_frames")
        cropped = expected.get("cropped_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_item_missing_medium" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code")) != \
                    (226, 0x63) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(exact, list) or \
                tuple(page.get("kind") for page in exact) != EXACT_KINDS or \
                tuple(page.get("rewrite_frame") for page in exact) != \
                    EXACT_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in exact) != \
                    EXACT_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in exact) != \
                    EXACT_STABLE_THROUGH or \
                not isinstance(cropped, list) or \
                tuple(page.get("kind") for page in cropped) != CROP_KINDS or \
                tuple(page.get("rewrite_frame") for page in cropped) != \
                    CROP_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in cropped) != \
                    CROP_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in cropped) != \
                    CROP_STABLE_THROUGH:
            raise ValueError("unsupported FIG item missing-medium reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from item missing-medium reference")
        image = image_bytes(original_executable)
        instruction_checks = (
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code",
             0x11fc, "e8b91be87801e8d15ce80d2ab80300e82f26"),
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
                    f"FIG item missing-medium instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG item missing-medium fixture differs")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG item missing-medium input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 5 or \
                expected.get("capture_time_limit_seconds") != 75 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 5250 or \
                expected.get("capture_video_frames") != 5256 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG item missing-medium capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "eight-pixel ZMBV artifact" not in limitation or \
                "top-197 crop" not in limitation or \
                "11fc" not in limitation or "58fa" not in limitation or \
                "0de5" not in limitation:
            raise ValueError("FIG item missing-medium limitation is missing")

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
            raise ValueError("FIG item missing-medium timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG item missing-medium final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG item missing-medium did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        timed_frames = (
            (121, "item_pose_at_milliseconds"),
            (126, "darkest_item_pose_at_milliseconds"),
            (127, "dark_missing_medium_at_milliseconds"),
            (132, "restored_missing_medium_at_milliseconds"),
            (133, "expiry_at_milliseconds"),
            (134, "clean_at_milliseconds"),
            (135, "monster_attack_at_milliseconds"),
            (156, "monster_tail_clean_at_milliseconds"),
            (157, "next_command_at_milliseconds"),
        )
        if any(frame_times[index] != expected[key]
               for index, key in timed_frames) or \
                frame_times[134] - frame_times[133] != \
                    expected["expiry_hold_milliseconds"] or \
                expected["expiry_hold_milliseconds"] != 989 or \
                frame_times[135] - frame_times[134] != 274 or \
                frame_times[157] - frame_times[156] != 110:
            raise ValueError("FIG item missing-medium event timing differs")
        voices = [item for item in trace.get("timeline", [])
                  if item.get("kind") == "voice"]
        if voices[7].get("at_milliseconds") != 10381 or \
                voices[7].get("payload_fnv1a64") != "2956be0892aeb426" or \
                voices[8].get("at_milliseconds") != 12633 or \
                voices[8].get("payload_fnv1a64") != "ce3659387971554b":
            raise ValueError("FIG item missing-medium voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG item missing-medium frame count differs")
        for page in exact:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG item missing-medium exact page differs: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        for page in cropped:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            crop = rgb[:320 * 197 * 3]
            if page.get("crop") != [0, 0, 320, 197] or \
                    page.get("mismatched_full_rgb_pixels") != 8 or \
                    page.get("mismatched_full_rgb_bbox") != [72, 197, 75, 198] or \
                    sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_full_rgb_sha256"] or \
                    sha256(crop) != page["rewrite_crop_rgb_sha256"] or \
                    sha256(crop) != page["original_crop_rgb_sha256"] or \
                    page["rewrite_full_rgb_sha256"] == \
                        page["original_full_rgb_sha256"]:
                raise ValueError(
                    "FIG item missing-medium cropped page differs: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
            digest(page.get("original_full_rgb_sha256"),
                   page["kind"] + "/original_full_rgb_sha256")

        # The direct-item path at 11fc owns a source-anchored pose-zero page.
        # 0da7 must retain that exact context rather than using the learned
        # pose-four context or item 192's selected-target anchor.
        pose_pixels = frames[121][0]
        expiry_pixels = frames[133][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(zip(pose_pixels, expiry_pixels))
            if left != right
        ]
        bbox = (
            min(x for x, _ in changed), min(y for _, y in changed),
            max(x for x, _ in changed), max(y for _, y in changed))
        if len(changed) != expected["changed_from_pose_zero_pixels"] or \
                list(bbox) != expected["changed_from_pose_zero_bbox"] or \
                len(changed) != 2560 or bbox != (16, 120, 95, 151) or \
                frames[134] != frames[156] or frames[132] == frames[133] or \
                frames[133] == frames[134]:
            raise ValueError("FIG item expiry lost source-anchored pose zero")

        print(
            "FIG item missing-medium expiry checkpoint: item 226 retains "
            "source-anchored pose zero for attack-buff expiry, holds 18 ticks, "
            "and matches six full original RGB pages plus three top-197 crops")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG item missing-medium expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
