#!/usr/bin/env python3
"""Lock learned ability 85's double missing-medium composite and expiry."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames

EXPECTED_KINDS = (
    "learned_pose_zero", "learned_pose_four",
    "darkening_step_two", "darkening_step_three", "darkening_step_four",
    "darkest_pose_four", "first_missing_medium_card",
    "second_missing_medium_card", "attack_buff_expired",
    "expiry_tail_clean", "monster_attack_card",
) + tuple(f"monster_shake{index}" for index in range(1, 8)) + (
    "pre_damage_reaction",
) + tuple(f"damage_rise{index}" for index in range(1, 11)) + (
    "monster_action_tail", "round_boundary", "next_command",
)
EXPECTED_REWRITE = (121, 122) + tuple(range(124, 130)) + \
    tuple(range(135, 145)) + tuple(range(146, 160))
REWRITE_ONLY_KINDS = (
    "ability_list_boundary", "source_anchor_transition",
    "source_anchor_confirm_transition", "first_darkening_step",
    "restoration_step_one", "restoration_step_two",
    "restoration_step_three", "restoration_step_four",
    "restoration_step_five", "final_shake_alternate", "quit_boundary",
)
REWRITE_ONLY_FRAMES = (118, 119, 120, 123, 130, 131, 132, 133, 134,
                       145, 160)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(
            f"malformed learned-missing-media-composite digest {label}")
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
        rewrite_only = expected.get("rewrite_only_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_ability_composite_missing_media" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("finishing_ability_id"),
                 expected.get("finishing_effect_code"),
                 expected.get("finishing_target_mode"),
                 expected.get("nested_effects"),
                 expected.get("nested_resolution"),
                 expected.get("resource_cost"),
                 expected.get("payment_pool")) != \
                    (85, 0x6b, 0, [0x43, 0x36], "missing_medium", 60,
                     "ability_points") or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 32 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                not isinstance(rewrite_only, list) or len(rewrite_only) != 11 or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError(
                "unsupported learned-missing-media-composite reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError(
                "FIG.EXE differs from learned-missing-media reference")
        image = image_bytes(original)
        checks = (
            ("learned_ability_pose", 0x4338),
            ("composite_handler", 0x57f2),
            ("missing_medium_handler", 0x58fa),
            ("common_debit", 0x585e),
            ("dispatcher_restore", 0x4417),
            ("player_expiry_dispatch", 0x0c41),
            ("expiry_card", 0x0da7),
            ("cleanup_tail", 0x0d98),
        )
        for stem, offset in checks:
            code = expected.get(stem + "_machine_code")
            if expected.get(stem + "_image_offset") != offset or \
                    not isinstance(code, str) or not code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    "FIG learned-missing-media instruction differs: " + stem)
        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError(
                "ITEM.EXE differs from learned-missing-media reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG learned-missing-media fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG learned-missing-media inputs differ")
        if (expected.get("capture_wait_seconds"),
                expected.get("capture_pace_seconds"),
                expected.get("capture_time_limit_seconds"),
                expected.get("capture_review_fps"),
                expected.get("capture_review_frames"),
                expected.get("capture_video_frames"),
                expected.get("capture_dosbox_exit_code")) != \
                (5, 5, 75, 70, 5250, 5256, 0) or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError(
                "FIG learned-missing-media capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "source-anchored" not in limitation or \
                "two 58fa" not in limitation or \
                "sole 585e payment and 4417 restoration" not in limitation or \
                "deliberately unpaired" not in limitation:
            raise ValueError(
                "FIG learned-missing-media limitation is missing")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.game), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path), "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError(
                "FIG learned-missing-media rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True,
                }]:
            raise ValueError("FIG learned-missing-media replay exit differs")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (121, "pose_zero_at_milliseconds"),
            (122, "pose_four_at_milliseconds"),
            (127, "darkest_pose_at_milliseconds"),
            (128, "first_missing_card_at_milliseconds"),
            (129, "second_missing_card_at_milliseconds"),
            (130, "first_restoration_at_milliseconds"),
            (134, "last_restoration_at_milliseconds"),
            (135, "expiry_at_milliseconds"),
            (136, "clean_at_milliseconds"),
            (137, "monster_attack_at_milliseconds"),
            (157, "monster_tail_at_milliseconds"),
            (158, "round_boundary_at_milliseconds"),
            (159, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                times[129] - times[128] != \
                    expected["missing_card_hold_milliseconds"] or \
                times[130] - times[129] != \
                    expected["missing_card_hold_milliseconds"] or \
                any(times[index + 1] - times[index] !=
                    expected["restoration_step_milliseconds"]
                    for index in range(130, 135)) or \
                times[136] - times[135] != \
                    expected["expiry_hold_milliseconds"] or \
                times[137] - times[136] != expected["clean_hold_milliseconds"]:
            raise ValueError(
                "FIG learned-missing-media timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 10 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (expected["first_missing_voice_at_milliseconds"],
                     "2956be0892aeb426") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (expected["second_missing_voice_at_milliseconds"],
                     "2956be0892aeb426") or \
                (voices[9].get("at_milliseconds"),
                 voices[9].get("payload_fnv1a64")) != \
                    (expected["monster_voice_at_milliseconds"],
                     "ce3659387971554b"):
            raise ValueError(
                "FIG learned-missing-media voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError(
                "FIG learned-missing-media frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG learned-missing-media exact page differs: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        for page in rewrite_only:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"]:
                raise ValueError(
                    "FIG learned-missing-media observation differs: " +
                    page["kind"])

        print(
            "FIG learned missing-media composite expiry checkpoint: 32 exact "
            "pages lock one source-anchored pose/dark envelope, two 58fa "
            "cards, sole payment/restoration and same-turn expiry")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, "FIG learned missing-media composite expiry checkpoint: "
            f"FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
