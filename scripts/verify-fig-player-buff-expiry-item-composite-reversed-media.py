#!/usr/bin/env python3
"""Lock item 201's reversed AF/AE flights followed by same-turn buff expiry."""

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
) + tuple(f"first_medium_flight{index}" for index in range(1, 28)) + \
    tuple(f"second_medium_flight{index}" for index in range(1, 21)) + (
        "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
    ) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
        "monster_action_tail", "round_boundary", "next_command",
    )
EXPECTED_REWRITE = (120, 122, 123, 124, 125) + tuple(range(127, 154)) + \
    tuple(range(154, 174)) + (180, 181, 182) + tuple(range(183, 190)) + \
    tuple(range(192, 202)) + (202, 203, 204)
CROPPED_KINDS = (
    "restoration_step_one", "restoration_step_two",
    "restoration_step_three", "restoration_step_four",
    "restored_two_media",
)
CROPPED_REWRITE = (175, 176, 177, 178, 179)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "first_flight_frame_zero",
    "paid_dark_clean", "final_shake_alternate", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (121, 126, 174, 190, 191)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed composite-reversed-media-expiry digest {label}")
    return value


def image_bytes(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid MZ header")
    return executable[header:]


def archive_record(executable: bytes, index: int) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError("ITEM.EXE is not an MZ archive")
    header = int.from_bytes(executable[8:10], "little") * 16
    image = executable[header:]
    start = int.from_bytes(image[index * 2:index * 2 + 2], "little")
    directory_bytes = int.from_bytes(image[2:4], "little")
    sentinel = int.from_bytes(image[:2], "little")
    if directory_bytes < 4 or index * 2 + 2 > directory_bytes:
        raise ValueError("ITEM.EXE directory is truncated")
    offsets = [int.from_bytes(image[pos:pos + 2], "little")
               for pos in range(0, directory_bytes, 2)]
    finish = min((offset for offset in offsets if offset > start),
                 default=sentinel)
    if start >= finish or finish > len(image):
        raise ValueError("ITEM.EXE record bounds are invalid")
    return image[start:finish]


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
        cropped = expected.get("cropped_frames")
        rewrite_only = expected.get("rewrite_only_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_item_composite_reversed_media" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_type"),
                 expected.get("item_use_flags"),
                 expected.get("item_target_flags"),
                 expected.get("item_effect_code"),
                 expected.get("embedded_ability_id"),
                 expected.get("embedded_target_flags"),
                 expected.get("nested_effects"),
                 expected.get("installed_media"),
                 expected.get("action_anchor"),
                 expected.get("resource_cost"),
                 expected.get("payment_pool")) != \
                    (201, 0x10, 0x6e, 0, 0x6b, 61, 0x8100,
                     [0x3b, 0x31], [1, 0], "source_actor", 36,
                     "ability_points") or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 75 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                not isinstance(cropped, list) or len(cropped) != 5 or \
                tuple(page.get("kind") for page in cropped) != CROPPED_KINDS or \
                tuple(page.get("rewrite_frame") for page in cropped) != \
                    CROPPED_REWRITE or \
                not isinstance(rewrite_only, list) or len(rewrite_only) != 5 or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError("unsupported FIG composite-reversed-media-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from composite-reversed-media-expiry reference")
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
                    "FIG composite-reversed-media-expiry instruction differs: " + code_key)
        item_archive = (args.game / "ITEM.EXE").read_bytes()
        if sha256(item_archive) != expected["item_archive_sha256"]:
            raise ValueError("ITEM.EXE differs from composite-reversed-media-expiry reference")
        item = archive_record(item_archive, 201 + 2)
        if len(item) < 11 or \
                (int.from_bytes(item[0:2], "little"), item[5], item[6],
                 int.from_bytes(item[7:9], "little"), item[9], item[10]) != \
                    (0x10, 0x6e, 0, 0x6b, 0x3b, 0x31):
            raise ValueError("ITEM.EXE item 201 composite record differs")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG composite-reversed-media-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG composite-reversed-media-expiry inputs differ")
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
            raise ValueError("FIG composite-reversed-media-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Seventy-five stable full 320x200 RGB pages" not in limitation or \
                "five exact top-197 crops" not in limitation or \
                "eight-pixel ZMBV bottom-scanline artifact" not in limitation or \
                "source-actor-anchored" not in limitation or \
                "3Bh installs AF" not in limitation or \
                "31h installs AE" not in limitation or \
                "36-point" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "57f2", "4792", "5b41",
                    "0c41", "0da7", "0d98")):
            raise ValueError("FIG composite-reversed-media-expiry limitation is missing")

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
            raise ValueError("FIG composite-reversed-media-expiry rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG composite-reversed-media-expiry missed replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = ((120, "item_pose_at_milliseconds"),
                 (153, "last_first_flight_at_milliseconds"),
                 (154, "first_second_flight_at_milliseconds"),
                 (173, "last_second_flight_at_milliseconds"),
                 (174, "paid_dark_at_milliseconds"),
                 (179, "restored_two_media_at_milliseconds"),
                 (180, "expiry_at_milliseconds"), (181, "clean_at_milliseconds"),
                 (182, "monster_attack_at_milliseconds"),
                 (202, "monster_tail_at_milliseconds"),
                 (203, "round_boundary_at_milliseconds"),
                 (204, "next_command_at_milliseconds"))
        if any(times[index] != expected[key] for index, key in timed) or \
                times[180] - times[179] != 55 or \
                times[181] - times[180] != expected["expiry_hold_milliseconds"] or \
                times[182] - times[181] != expected["clean_hold_milliseconds"]:
            raise ValueError("FIG composite-reversed-media-expiry timing differs")
        voices = [entry for entry in trace["timeline"] if entry.get("kind") == "voice"]
        if len(voices) != 10 or \
                (voices[7].get("at_milliseconds"), voices[7].get("payload_fnv1a64")) != \
                    (expected["first_medium_voice_at_milliseconds"], "56054b2c8ee75346") or \
                (voices[8].get("at_milliseconds"), voices[8].get("payload_fnv1a64")) != \
                    (expected["second_medium_voice_at_milliseconds"], "56054b2c8ee75346") or \
                (voices[9].get("at_milliseconds"), voices[9].get("payload_fnv1a64")) != \
                    (expected["monster_voice_at_milliseconds"], "ce3659387971554b"):
            raise ValueError("FIG composite-reversed-media-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG composite-reversed-media-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError("FIG composite-reversed-media-expiry exact page differs: " + page["kind"])
            digest(page.get("original_png_sha256"), page["kind"] + "/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int) or \
                    page.get("original_stable_through", -1) < \
                        page["original_review_frame"]:
                raise ValueError(
                    "FIG composite-reversed-media-expiry original range differs: " +
                    page["kind"])
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
                    "FIG composite-reversed-media-expiry crop differs: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
            digest(page.get("original_full_rgb_sha256"),
                   page["kind"] + "/original_full_rgb_sha256")
        for page in rewrite_only:
            pixels, palette = frames[page["rewrite_frame"]]
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(expand_rgb(pixels, palette)) != page["rewrite_rgb_sha256"] or \
                    "original_rgb_sha256" in page:
                raise ValueError("FIG composite-reversed-media-expiry rewrite-only page differs: " + page["kind"])
        if frames[179] == frames[180] or frames[180] == frames[181]:
            raise ValueError("FIG composite-reversed-media-expiry retained-page ownership differs")

        print(
            "FIG composite-reversed-media expiry checkpoint: 75 exact RGB "
            "pages and five exact top-197 crops lock AF-before-AE flights, "
            "retained media, same-turn expiry and following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG composite-reversed-media expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
