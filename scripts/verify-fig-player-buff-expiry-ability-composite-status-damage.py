#!/usr/bin/env python3
"""Lock learned ability 80's resisted-then-damage composite and expiry."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames

EXPECTED_KINDS = (
    "learned_pose_zero", "learned_pose_four",
) + tuple(f"first_nested_page{index}" for index in range(2, 20)) + \
    tuple(f"second_nested_page{index}" for index in range(1, 21)) + (
        "damage_reaction",
    ) + tuple(f"damage_rise{index}" for index in range(2, 11)) + (
        "restoration_step_two", "restoration_step_three",
        "restoration_step_four", "restored_pose_four",
        "attack_buff_expired", "expiry_tail_clean", "round_boundary",
        "next_command",
    )
EXPECTED_REWRITE = (121, 122) + tuple(range(124, 142)) + \
    tuple(range(142, 162)) + (162,) + tuple(range(164, 173)) + \
    tuple(range(176, 184))
REWRITE_ONLY_KINDS = (
    "ability_list_boundary", "target_selector", "target_confirm_transition",
    "first_nested_page1", "damage_rise1", "handler_dark_clean",
    "paid_dark_clean", "restoration_step_one", "quit_boundary",
)
REWRITE_ONLY_FRAMES = (118, 119, 120, 123, 163, 173, 174, 175, 184)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(
            f"malformed learned-status-damage-composite digest {label}")
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
                    "original_fig_player_buff_expiry_after_ability_composite_status_damage" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("finishing_ability_id"),
                 expected.get("finishing_effect_code"),
                 expected.get("nested_effects"),
                 expected.get("first_nested_resolution"),
                 expected.get("resource_cost"),
                 expected.get("payment_pool")) != \
                    (80, 0x6b, [0x60, 0x46], "resistance", 60,
                     "ability_points") or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 58 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                not isinstance(rewrite_only, list) or len(rewrite_only) != 9 or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError(
                "unsupported learned-status-damage-composite reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError(
                "FIG.EXE differs from learned-status-damage reference")
        image = image_bytes(original)
        checks = (
            ("learned_ability_pose", 0x4338),
            ("composite_handler", 0x57f2),
            ("resistance_handler", 0x59a1),
            ("damage_result", 0x144e),
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
                    "FIG learned-status-damage instruction differs: " + stem)
        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError(
                "ITEM.EXE differs from learned-status-damage reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG learned-status-damage fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG learned-status-damage inputs differ")
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
                "FIG learned-status-damage capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "without intermediate 585e/4417" not in limitation or \
                "deliberately unpaired" not in limitation:
            raise ValueError(
                "FIG learned-status-damage limitation is missing")

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
                "FIG learned-status-damage rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True,
                }]:
            raise ValueError("FIG learned-status-damage replay exit differs")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (121, "pose_zero_at_milliseconds"),
            (122, "pose_four_at_milliseconds"),
            (142, "second_nested_first_page_at_milliseconds"),
            (162, "damage_reaction_at_milliseconds"),
            (179, "restored_at_milliseconds"),
            (180, "expiry_at_milliseconds"),
            (181, "clean_at_milliseconds"),
            (182, "round_boundary_at_milliseconds"),
            (183, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                times[180] - times[179] != 55 or \
                times[181] - times[180] != expected["expiry_hold_milliseconds"] or \
                times[182] - times[181] != expected["clean_hold_milliseconds"]:
            raise ValueError(
                "FIG learned-status-damage timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 9 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (expected["first_nested_voice_at_milliseconds"],
                     "c5f6abaeb6fdd610") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (expected["second_nested_voice_at_milliseconds"],
                     "2812420cc349abf7"):
            raise ValueError(
                "FIG learned-status-damage voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError(
                "FIG learned-status-damage frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG learned-status-damage exact page differs: " +
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
                    "FIG learned-status-damage observation differs: " +
                    page["kind"])

        print(
            "FIG learned status+damage composite expiry checkpoint: 58 exact "
            "pages lock the resisted first selector, retained dark second "
            "selector, sole payment/restoration and same-turn expiry")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, "FIG learned status+damage composite expiry checkpoint: "
            f"FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
