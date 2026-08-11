#!/usr/bin/env python3
"""Lock item 230's two dark damage phases and same-turn buff expiry."""

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
) + tuple(f"first_effect_page{index}" for index in range(1, 11)) + \
    tuple(f"first_damage_rise{index}" for index in range(1, 10)) + \
    tuple(f"second_effect_page{index}" for index in range(1, 9)) + \
    tuple(f"second_damage_rise{index}" for index in range(1, 10)) + (
        "expiry_tail_clean", "monster_attack_card",
        "monster_shake_shift1", "monster_shake_shift2",
        "monster_shake_shift3", "round_boundary", "next_command",
    )
EXPECTED_REWRITE = (120, 122, 123, 124, 125) + tuple(range(126, 136)) + \
    tuple(range(137, 146)) + tuple(range(146, 154)) + \
    tuple(range(155, 164)) + (172, 174, 177, 179, 181, 196, 197)
CROPPED_KINDS = (
    "restored_item_pose_zero", "restoration_step_four",
    "restoration_step_five", "attack_buff_expired",
) + tuple(f"monster_shake_clean{index}" for index in range(1, 6)) + \
    tuple(f"monster_damage_rise{index}" for index in range(1, 11))
CROPPED_REWRITE = (170, 168, 169, 171, 176, 178, 180, 182, 184) + \
    tuple(range(185, 195))
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "first_damage_reaction",
    "second_damage_reaction", "restoration_step_one",
    "restoration_step_two", "monster_pre_shake_clean",
    "final_shake_alternate", "monster_action_tail",
)
REWRITE_ONLY_FRAMES = (121, 136, 154, 166, 167, 175, 183, 195)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed composite-damage-expiry digest {label}")
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
        cropped = expected.get("cropped_frames")
        rewrite_only = expected.get("rewrite_only_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_item_composite_damage" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code"),
                 expected.get("embedded_ability_id"),
                 expected.get("nested_effects"),
                 expected.get("resource_cost"),
                 expected.get("payment_pool")) != \
                    (230, 0x6b, 90, [0x38, 0x3a], 40, "ability_points") or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 48 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                not isinstance(cropped, list) or len(cropped) != 19 or \
                tuple(page.get("kind") for page in cropped) != CROPPED_KINDS or \
                tuple(page.get("rewrite_frame") for page in cropped) != \
                    CROPPED_REWRITE or \
                not isinstance(rewrite_only, list) or len(rewrite_only) != 8 or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError("unsupported FIG composite-damage-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from composite-damage reference")
        image = image_bytes(original)
        checks = (
            ("item_command", 0x0a84), ("direct_item_pose", 0x11fc),
            ("item_dispatch_tail", 0x1235), ("composite_handler", 0x57f2),
            ("dispatcher_darken", 0x43ce), ("damage_result", 0x144e),
            ("common_debit", 0x585e), ("dispatcher_restore", 0x4417),
            ("player_expiry_dispatch", 0x0c41), ("expiry_card", 0x0da7),
            ("cleanup_tail", 0x0d98),
        )
        for stem, offset in checks:
            code = expected.get(stem + "_machine_code")
            if expected.get(stem + "_image_offset") != offset or \
                    not isinstance(code, str) or not code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    "FIG composite-damage instruction differs: " + stem)
        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError("ITEM.EXE differs from composite-damage reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG composite-damage fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG composite-damage inputs differ")
        if (expected.get("capture_wait_seconds"),
                expected.get("capture_pace_seconds"),
                expected.get("capture_time_limit_seconds"),
                expected.get("capture_review_fps"),
                expected.get("capture_review_frames"),
                expected.get("capture_video_frames"),
                expected.get("capture_dosbox_exit_code")) != \
                (5, 5, 100, 70, 6999, 7008, 0) or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG composite-damage capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "without an intermediate 585e/4417" not in limitation or \
                "deliberately unpaired" not in limitation or \
                "ZMBV bottom-scanline artifact" not in limitation:
            raise ValueError("FIG composite-damage limitation is missing")

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
            raise ValueError("FIG composite-damage rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG composite-damage replay exit differs")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = ((120, "item_pose_at_milliseconds"),
                 (146, "second_nested_first_page_at_milliseconds"),
                 (170, "restored_at_milliseconds"),
                 (171, "expiry_at_milliseconds"),
                 (172, "clean_at_milliseconds"),
                 (174, "monster_attack_at_milliseconds"),
                 (195, "monster_tail_at_milliseconds"),
                 (196, "round_boundary_at_milliseconds"),
                 (197, "next_command_at_milliseconds"))
        if any(times[index] != expected[key] for index, key in timed) or \
                times[171] - times[170] != 55 or \
                times[172] - times[171] != expected["expiry_hold_milliseconds"] or \
                times[174] - times[172] != expected["clean_hold_milliseconds"]:
            raise ValueError("FIG composite-damage timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 10 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (expected["first_nested_voice_at_milliseconds"],
                     "91632f0b893bbd13") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (expected["second_nested_voice_at_milliseconds"],
                     "fdf5c1bce1865077") or \
                (voices[9].get("at_milliseconds"),
                 voices[9].get("payload_fnv1a64")) != \
                    (expected["monster_voice_at_milliseconds"],
                     "7b151dcc4eff4103"):
            raise ValueError("FIG composite-damage voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG composite-damage frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG composite-damage exact page differs: " + page["kind"])
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
                    "FIG composite-damage crop differs: " + page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
            digest(page.get("original_full_rgb_sha256"),
                   page["kind"] + "/original_full_rgb_sha256")
        for page in rewrite_only:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"]:
                raise ValueError(
                    "FIG composite-damage observation differs: " + page["kind"])

        print(
            "FIG composite-damage expiry checkpoint: 48 exact pages and "
            "19 top-197 crops lock the shared dark/payment envelope, "
            "same-turn expiry and following monster action")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG composite-damage expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
