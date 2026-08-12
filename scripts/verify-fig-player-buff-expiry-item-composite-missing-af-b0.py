#!/usr/bin/env python3
"""Lock item 215's missing-AF/missing-B0 composite envelope and buff expiry."""

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
    "missing_af_card", "missing_b0_card",
    "restoration_step_two", "restoration_step_three",
    "restoration_step_four", "restored_missing_medium_card",
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
    "monster_shake_clean1", "monster_shake_shift1",
    "monster_shake_clean2", "monster_shake_shift2",
    "monster_shake_clean3", "monster_shake_shift3",
    "monster_shake_clean4", "pre_damage_clean",
) + tuple(f"monster_damage_rise{index}" for index in range(1, 11)) + (
    "monster_action_tail", "round_boundary", "next_command",
)
EXPECTED_REWRITE = (120,) + tuple(range(122, 128)) + tuple(range(129, 143)) + \
    tuple(range(144, 158))
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "restoration_step_one",
    "final_shake_alternate",
)
REWRITE_ONLY_FRAMES = (121, 128, 143)

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed missing-AF-B0 digest {label}")
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
    directory_bytes = int.from_bytes(image[2:4], "little")
    sentinel = int.from_bytes(image[:2], "little")
    if directory_bytes < 4 or index * 2 + 2 > directory_bytes:
        raise ValueError("ITEM.EXE directory is truncated")
    offsets = [int.from_bytes(image[pos:pos + 2], "little")
               for pos in range(0, directory_bytes, 2)]
    start = offsets[index]
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
        rewrite_only = expected.get("rewrite_only_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_item_composite_missing_af_b0" or \
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
                 expected.get("nested_effects")) != \
                    (215, 0x10, 0x6e, 0, 0x6b, 75, 0x8400,
                     [0x43, 0x45]) or \
                (expected.get("nested_resolution"),
                 expected.get("action_anchor"), expected.get("resource_cost"),
                 expected.get("payment_pool"),
                 expected.get("consumes_inventory")) != \
                    (["missing_af_medium", "missing_b0_medium"],
                     "source_actor", 0, "none", True) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 35 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                not isinstance(rewrite_only, list) or len(rewrite_only) != 3 or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError("unsupported FIG missing-AF/B0 item reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from missing-AF/B0 reference")
        image = image_bytes(original)
        checks = (
            ("item_command", 0x0a84), ("direct_item_pose", 0x11fc),
            ("item_dispatch_tail", 0x1235), ("composite_handler", 0x57f2),
            ("dispatcher_darken", 0x43ce),
            ("missing_medium_handler", 0x58fa), ("common_debit", 0x585e),
            ("dispatcher_restore", 0x4417),
            ("player_expiry_dispatch", 0x0c41), ("expiry_card", 0x0da7),
            ("cleanup_tail", 0x0d98),
        )
        for stem, offset in checks:
            code = expected.get(stem + "_machine_code")
            if expected.get(stem + "_image_offset") != offset or \
                    not isinstance(code, str) or not code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    "FIG missing-AF/B0 instruction differs: " + stem)

        item_archive = (args.game / "ITEM.EXE").read_bytes()
        if sha256(item_archive) != expected["item_archive_sha256"]:
            raise ValueError("ITEM.EXE differs from missing-AF/B0 fixture")
        item = archive_record(item_archive, 215 + 2)
        if len(item) < 11 or \
                (int.from_bytes(item[0:2], "little"), item[5], item[6],
                 int.from_bytes(item[7:9], "little"), item[9], item[10]) != \
                    (0x10, 0x6e, 0, 0x6b, 0x43, 0x45):
            raise ValueError("ITEM.EXE item 215/monster patch differs")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG missing-AF/B0 fixture differs")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG missing-AF/B0 inputs differ")
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
            raise ValueError("FIG missing-AF/B0 capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Thirty-five stable full 320x200 RGB pages" not in limitation or \
                "source-actor-anchored" not in limitation or \
                "absent AF" not in limitation or \
                "absent B0" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "57f2", "43ce", "58fa", "585e", "4417", "0c41", "0da7", "0d98")):
            raise ValueError("FIG missing-AF/B0 limitation is missing")

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
            raise ValueError("FIG missing-AF/B0 rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG missing-AF/B0 replay exit differs")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (120, "item_pose_at_milliseconds"),
            (125, "darkest_pose_at_milliseconds"),
            (126, "first_missing_card_at_milliseconds"),
            (127, "second_missing_card_at_milliseconds"),
            (128, "first_restoration_at_milliseconds"),
            (132, "restored_at_milliseconds"),
            (133, "expiry_at_milliseconds"),
            (134, "clean_at_milliseconds"),
            (135, "monster_attack_at_milliseconds"),
            (155, "monster_tail_at_milliseconds"),
            (156, "round_boundary_at_milliseconds"),
            (157, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                times[127] - times[126] != \
                    expected["missing_card_hold_milliseconds"] or \
                times[134] - times[133] != expected["expiry_hold_milliseconds"] or \
                times[135] - times[134] != expected["clean_hold_milliseconds"]:
            raise ValueError("FIG missing-AF/B0 timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        voice_checks = (
            (7, "first_missing_voice_at_milliseconds", "2956be0892aeb426"),
            (8, "second_missing_voice_at_milliseconds", "2956be0892aeb426"),
            (9, "monster_voice_at_milliseconds", "ce3659387971554b"),
        )
        if len(voices) != 10 or any(
                (voices[index].get("at_milliseconds"),
                 voices[index].get("payload_fnv1a64")) !=
                (expected[key], payload)
                for index, key, payload in voice_checks):
            raise ValueError("FIG missing-AF/B0 voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG missing-AF/B0 frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG missing-AF/B0 exact page differs: " + page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int) or \
                    page.get("original_stable_through", -1) < \
                        page["original_review_frame"]:
                raise ValueError(
                    "FIG missing-AF/B0 original range differs: " +
                    page["kind"])
        for page in rewrite_only:
            pixels, palette = frames[page["rewrite_frame"]]
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(expand_rgb(pixels, palette)) != \
                        page["rewrite_rgb_sha256"] or \
                    "original_rgb_sha256" in page:
                raise ValueError(
                    "FIG missing-AF/B0 observation differs: " + page["kind"])
        if frames[126] != frames[127] or frames[128] == frames[129]:
            raise ValueError("FIG missing-AF/B0 grouped missing-card restoration differs")

        print(
            "FIG missing-AF/B0 item composite expiry checkpoint: 35 exact "
            "pages lock the grouped AF/B0 58fa envelope and same-turn expiry")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, "FIG missing-AF/B0 item composite expiry checkpoint: "
            f"FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
