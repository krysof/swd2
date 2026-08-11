#!/usr/bin/env python3
"""Lock item 195's two medium flights followed by same-turn buff expiry."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "item_pose_zero", "darkening_step_two", "darkening_step_three",
    "darkening_step_four", "darkest_item_pose_zero",
) + tuple(f"first_medium_flight{index}" for index in range(1, 20)) + \
    tuple(f"second_medium_flight{index}" for index in range(1, 25)) + (
        "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
    ) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
        "monster_action_tail", "round_boundary", "next_command",
    )
EXPECTED_REWRITE = (120, 122, 123, 124, 125) + tuple(range(128, 147)) + \
    tuple(range(147, 171)) + (177, 178, 179) + tuple(range(180, 187)) + \
    tuple(range(189, 199)) + (199, 200, 201)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "first_flight_frame_zero",
    "first_flight_live_transition",
) + tuple(f"restoration_step{index}" for index in range(1, 6)) + (
    "restored_two_media", "final_shake_alternate", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (121, 126, 127, 171, 172, 173, 174, 175, 176, 187, 188)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed composite-media-expiry digest {label}")
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
                    "original_fig_player_buff_expiry_after_item_composite_media" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code"),
                 expected.get("embedded_ability_id"),
                 expected.get("nested_effects"),
                 expected.get("installed_media")) != \
                    (195, 0x6b, 55, [0x31, 0x3c], [0, 2]) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 71 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                not isinstance(rewrite_only, list) or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError("unsupported FIG composite-media-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from composite-media-expiry reference")
        image = image_bytes(original)
        checks = (
            ("item_command_image_offset", "item_command_machine_code", 0x0a84),
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code", 0x11fc),
            ("item_dispatch_tail_image_offset", "item_dispatch_tail_machine_code", 0x1235),
            ("composite_handler_image_offset", "composite_handler_machine_code", 0x57f2),
            ("medium_install_handler_image_offset", "medium_install_handler_machine_code", 0x4792),
            ("medium_flight_image_offset", "medium_flight_machine_code", 0x5b41),
            ("player_expiry_dispatch_image_offset", "player_expiry_dispatch_machine_code", 0x0c41),
            ("expiry_card_image_offset", "expiry_card_machine_code", 0x0da7),
            ("cleanup_tail_image_offset", "cleanup_tail_machine_code", 0x0d98),
        )
        for offset_key, code_key, offset in checks:
            code = expected.get(code_key)
            if expected.get(offset_key) != offset or not isinstance(code, str) or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    "FIG composite-media-expiry instruction differs: " + code_key)
        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError("ITEM.EXE differs from composite-media-expiry reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG composite-media-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG composite-media-expiry inputs differ")
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
            raise ValueError("FIG composite-media-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Seventy-one stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "57f2", "4792", "5b41",
                    "0c41", "0da7", "0d98")):
            raise ValueError("FIG composite-media-expiry limitation is missing")

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
                trace.get("delay_milliseconds") != expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"], expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG composite-media-expiry rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG composite-media-expiry missed replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = ((120, "item_pose_at_milliseconds"),
                 (146, "last_first_flight_at_milliseconds"),
                 (147, "first_second_flight_at_milliseconds"),
                 (170, "last_second_flight_at_milliseconds"),
                 (171, "first_restoration_at_milliseconds"),
                 (176, "restored_two_media_at_milliseconds"),
                 (177, "expiry_at_milliseconds"), (178, "clean_at_milliseconds"),
                 (179, "monster_attack_at_milliseconds"),
                 (199, "monster_tail_at_milliseconds"),
                 (200, "round_boundary_at_milliseconds"),
                 (201, "next_command_at_milliseconds"))
        if any(times[index] != expected[key] for index, key in timed) or \
                times[177] - times[176] != 55 or \
                times[178] - times[177] != expected["expiry_hold_milliseconds"] or \
                times[179] - times[178] != expected["clean_hold_milliseconds"]:
            raise ValueError("FIG composite-media-expiry timing differs")
        voices = [entry for entry in trace["timeline"] if entry.get("kind") == "voice"]
        if len(voices) != 10 or \
                (voices[7].get("at_milliseconds"), voices[7].get("payload_fnv1a64")) != \
                    (expected["first_medium_voice_at_milliseconds"], "56054b2c8ee75346") or \
                (voices[8].get("at_milliseconds"), voices[8].get("payload_fnv1a64")) != \
                    (expected["second_medium_voice_at_milliseconds"], "56054b2c8ee75346") or \
                (voices[9].get("at_milliseconds"), voices[9].get("payload_fnv1a64")) != \
                    (expected["monster_voice_at_milliseconds"], "ce3659387971554b"):
            raise ValueError("FIG composite-media-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG composite-media-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError("FIG composite-media-expiry exact page differs: " + page["kind"])
            digest(page.get("original_png_sha256"), page["kind"] + "/original_png_sha256")
        for page in rewrite_only:
            pixels, palette = frames[page["rewrite_frame"]]
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(expand_rgb(pixels, palette)) != page["rewrite_rgb_sha256"] or \
                    "original_rgb_sha256" in page:
                raise ValueError("FIG composite-media-expiry rewrite-only page differs: " + page["kind"])
        if frames[176] == frames[177] or frames[177] == frames[178]:
            raise ValueError("FIG composite-media-expiry retained-page ownership differs")

        print("FIG composite-media expiry checkpoint: 71 original RGB pages lock both flights, retained media, same-turn expiry and the sole following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG composite-media expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
