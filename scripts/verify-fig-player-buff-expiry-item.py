#!/usr/bin/env python3
"""Lock FIG player attack-buff expiry after direct item 192."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXACT_KINDS = ("action_tail_clean", "monster_tail_clean", "next_command")
CROP_KINDS = (
    "retained_item_pose0", "attack_buff_expired",
    "following_monster_action_card",
)
EXACT_FRAMES = (158, 180, 181)
CROP_FRAMES = (156, 157, 159)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item-expiry digest {label}")
    return value


def image_bytes(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid header size")
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
                    "original_fig_player_buff_expiry_after_item" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code")) != \
                    (192, 0x33) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(exact, list) or \
                tuple(page.get("kind") for page in exact) != EXACT_KINDS or \
                tuple(page.get("rewrite_frame") for page in exact) != \
                    EXACT_FRAMES or \
                not isinstance(cropped, list) or \
                tuple(page.get("kind") for page in cropped) != CROP_KINDS or \
                tuple(page.get("rewrite_frame") for page in cropped) != \
                    CROP_FRAMES:
            raise ValueError("unsupported FIG item-expiry reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the item-expiry reference")
        offset = expected.get("expiry_wait_image_offset")
        machine_code = expected.get("expiry_wait_machine_code")
        if offset != 0x0de5 or machine_code != "b81200e8522a" or \
                expected.get("expiry_wait_ticks") != 18 or \
                image_bytes(original_executable)[offset:offset + 6].hex() != \
                    machine_code:
            raise ValueError("FIG 0da7 wait instruction differs")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG item-expiry fixture differs")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG item-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 5 or \
                expected.get("capture_time_limit_seconds") != 75 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 5250 or \
                expected.get("capture_video_frames") != 5256 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG item-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "eight-pixel ZMBV artifact" not in limitation or \
                "top-197 crop" not in limitation or \
                "FIG.EXE image bytes at 0de5" not in limitation:
            raise ValueError("FIG item-expiry capture limitation is missing")

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
            raise ValueError("FIG item-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG item-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG item-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        retained_frame = expected["retained_pose_rewrite_frame"]
        expiry_frame = expected["expiry_rewrite_frame"]
        clean_frame = expected["clean_rewrite_frame"]
        following_frame = expected["following_action_rewrite_frame"]
        if (retained_frame, expiry_frame, clean_frame, following_frame) != \
                (156, 157, 158, 159) or \
                frame_times[expiry_frame] != expected["expiry_at_milliseconds"] or \
                frame_times[clean_frame] != expected["clean_at_milliseconds"] or \
                frame_times[following_frame] != \
                    expected["following_action_at_milliseconds"] or \
                frame_times[clean_frame] - frame_times[expiry_frame] != \
                    expected["expiry_hold_milliseconds"] or \
                expected["expiry_hold_milliseconds"] != 988:
            raise ValueError("FIG item-expiry event timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG item-expiry frame count differs")
        for page in exact:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG item-expiry exact page differs: " + page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        for page in cropped:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            crop = rgb[:320 * 197 * 3]
            if page.get("crop") != [0, 0, 320, 197] or \
                    page.get("mismatched_full_rgb_pixels") != 8 or \
                    sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_full_rgb_sha256"] or \
                    sha256(crop) != page["rewrite_crop_rgb_sha256"] or \
                    sha256(crop) != page["original_crop_rgb_sha256"] or \
                    page["rewrite_full_rgb_sha256"] == \
                        page["original_full_rgb_sha256"]:
                raise ValueError(
                    "FIG item-expiry cropped page differs: " + page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
            digest(page.get("original_full_rgb_sha256"),
                   page["kind"] + "/original_full_rgb_sha256")

        # Direct item 1138 never reaches learned pose four.  Relative to its
        # retained pose-zero page, 0da7 changes only the four-column compact
        # panel at the same selected-monster anchor.
        retained_pixels = frames[retained_frame][0]
        expiry_pixels = frames[expiry_frame][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(retained_pixels, expiry_pixels)) if left != right
        ]
        if len(changed) != 2560 or (
                min(x for x, _ in changed), min(y for _, y in changed),
                max(x for x, _ in changed), max(y for _, y in changed)) != \
                (120, 120, 199, 151):
            raise ValueError("FIG item-expiry lost pose-zero target anchor")

        print(
            "FIG item-expiry checkpoint: item 192 leaves pose zero at the "
            "selected target; 0da7 overlays attack-buff expiry for 18 ticks, "
            "then reaches the exact clean/command pages and top-197 original "
            "RGB action crops")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item-expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
