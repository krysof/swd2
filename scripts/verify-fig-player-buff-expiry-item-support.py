#!/usr/bin/env python3
"""Lock FIG attack-buff expiry after direct item 190 heals its user."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "item_pose_zero", "support_before_update", "support_after_update",
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
    "monster_tail_clean", "next_command",
)
EXPECTED_REWRITE_FRAMES = (121, 122, 123, 124, 125, 126, 147, 148)
EXPECTED_ORIGINAL_FRAMES = (3901, 3916, 3920, 3943, 3982, 4006, 4093, 4115)
EXPECTED_STABLE_THROUGH = (3915, 3919, 3942, 3981, 4005, 4023, 4113, 5250)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item-support-expiry digest {label}")
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
                    "original_fig_player_buff_expiry_after_item_support" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code")) != \
                    (190, 0x01) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in pages) != \
                    EXPECTED_STABLE_THROUGH:
            raise ValueError("unsupported FIG item-support-expiry reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from item-support-expiry reference")
        image = image_bytes(original_executable)
        instruction_checks = (
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code",
             0x11fc, "e8b91be87801e8d15ce80d2ab80300e82f26"),
            ("support_handler_image_offset", "support_handler_machine_code",
             0x447b,
             "c706b92b1900c706bb2b1900e8b713803e4935007506e82b14e91214c3"),
            ("common_debit_image_offset", "common_debit_machine_code",
             0x585e,
             "8b1e3a31d1e38b874b2f8bbf532f2905c38b44082500e03d0000752b83c3028b0083eb02f7e1b964"),
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
                    f"FIG item-support-expiry instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG item-support-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG item-support-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 5 or \
                expected.get("capture_time_limit_seconds") != 75 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 5250 or \
                expected.get("capture_video_frames") != 5256 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG item-support-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Eight stable pages are exact" not in limitation or \
                "not claimed" not in limitation or \
                "11fc" not in limitation or "447b" not in limitation or \
                "585e" not in limitation or "0de5" not in limitation or \
                "0d98" not in limitation:
            raise ValueError("FIG item-support-expiry limitation is missing")

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
            raise ValueError("FIG item-support-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG item-support-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG item-support-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        timed_frames = (
            (121, "item_pose_at_milliseconds"),
            (122, "support_before_at_milliseconds"),
            (123, "support_after_at_milliseconds"),
            (124, "expiry_at_milliseconds"),
            (125, "clean_at_milliseconds"),
            (126, "monster_attack_at_milliseconds"),
            (147, "monster_tail_clean_at_milliseconds"),
            (148, "next_command_at_milliseconds"),
        )
        if any(frame_times[index] != expected[key]
               for index, key in timed_frames) or \
                frame_times[122] - frame_times[121] != 165 or \
                frame_times[123] - frame_times[122] != 165 or \
                frame_times[124] - frame_times[123] != 494 or \
                frame_times[125] - frame_times[124] != 989 or \
                frame_times[126] - frame_times[125] != 275 or \
                frame_times[148] - frame_times[147] != 110 or \
                (expected.get("item_pose_hold_milliseconds"),
                 expected.get("support_pre_hold_milliseconds"),
                 expected.get("support_post_hold_milliseconds"),
                 expected.get("expiry_hold_milliseconds"),
                 expected.get("clean_hold_milliseconds")) != \
                    (165, 165, 494, 989, 275):
            raise ValueError("FIG item-support-expiry event timing differs")
        voices = [item for item in trace.get("timeline", [])
                  if item.get("kind") == "voice"]
        if voices[7].get("at_milliseconds") != 10106 or \
                voices[7].get("payload_fnv1a64") != "f0f35db044f8d79f" or \
                voices[8].get("at_milliseconds") != 12248 or \
                voices[8].get("payload_fnv1a64") != "ce3659387971554b":
            raise ValueError("FIG item-support-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG item-support-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG item-support-expiry page differs: " + page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        # 447b draws the two support pages and returns through 585e. The
        # following 0c41/0da7 expiry must replace only the retained 80x32
        # source action card above y=152. The remaining changes are the healed
        # HP and debited item gauges in the bottom status strip.
        pose_pixels = frames[121][0]
        expiry_pixels = frames[124][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(zip(pose_pixels, expiry_pixels))
            if left != right
        ]
        top_changed = [(x, y) for x, y in changed if y < 152]
        bbox = (
            min(x for x, _ in changed), min(y for _, y in changed),
            max(x for x, _ in changed), max(y for _, y in changed))
        top_bbox = (
            min(x for x, _ in top_changed), min(y for _, y in top_changed),
            max(x for x, _ in top_changed), max(y for _, y in top_changed))
        if len(changed) != expected["changed_from_pose_zero_pixels"] or \
                list(bbox) != expected["changed_from_pose_zero_bbox"] or \
                len(changed) != 3801 or bbox != (16, 120, 95, 198) or \
                len(top_changed) != \
                    expected["changed_top_from_pose_zero_pixels"] or \
                list(top_bbox) != expected["changed_top_from_pose_zero_bbox"] or \
                len(top_changed) != 2560 or \
                top_bbox != (16, 120, 95, 151) or \
                frames[125] != frames[147] or \
                frames[121] == frames[122] or frames[122] == frames[123] or \
                frames[123] == frames[124] or frames[124] == frames[125]:
            raise ValueError("FIG item-support expiry lost retained support page")

        print(
            "FIG item-support expiry checkpoint: item 190 retains its support "
            "surface through same-turn attack-buff expiry, delays the sole "
            "clean page until after 18 ticks, and matches eight original RGB "
            "pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item-support expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
