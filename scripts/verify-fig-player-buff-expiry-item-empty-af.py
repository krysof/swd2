#!/usr/bin/env python3
"""Lock FIG same-turn attack-buff expiry after item 193 finds AE empty."""

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
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
        "monster_action_tail", "round_boundary", "next_command",
    )
EXPECTED_REWRITE = (
    121, 123, 124, 125, 126, 132, 133, 134,
) + tuple(range(135, 142)) + tuple(range(144, 154)) + (154, 155, 156)
EXPECTED_ORIGINAL = (
    3903, 3913, 3917, 3921, 3925, 4004, 4074, 4097, 4115, 4117,
    4119, 4121, 4123, 4125, 4127, 4131, 4133, 4135, 4137, 4139,
    4141, 4143, 4145, 4147, 4151, 4153, 4185, 4206,
)
EXPECTED_STABLE = (
    3911, 3915, 3919, 3923, 3962, 4073, 4096, 4114, 4116, 4118,
    4120, 4122, 4124, 4126, 4130, 4132, 4134, 4136, 4138, 4140,
    4142, 4144, 4146, 4150, 4152, 4184, 4205, 5599,
)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "restoration_step1", "restoration_step2",
    "restoration_step3", "restoration_step4", "restored_pose_zero",
    "final_shake_alternate", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (122, 127, 128, 129, 130, 131, 142, 143)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed empty-medium-expiry digest {label}")
    return value


def image_bytes(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid MZ header")
    return executable[header:]


def changed_box(left: bytes, right: bytes) -> tuple[int, list[int]]:
    changed = [(index % 320, index // 320)
               for index, pair in enumerate(zip(left, right))
               if pair[0] != pair[1]]
    return len(changed), [
        min(x for x, _ in changed), min(y for _, y in changed),
        max(x for x, _ in changed), max(y for _, y in changed),
    ]


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
                    "original_fig_player_buff_expiry_after_empty_af_medium_item" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code"),
                 expected.get("embedded_ability_id")) != (207, 0x3e, 67) or \
                expected.get("medium_initially_present") is not False or \
                (expected.get("medium_slot"),
                 expected.get("medium_sprite_id"),
                 expected.get("expiry_player_turn")) != (1, 0xaf, 4) or \
                not isinstance(pages, list) or len(pages) != 28 or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL or \
                tuple(page.get("original_stable_through") for page in pages) != \
                    EXPECTED_STABLE or \
                not isinstance(rewrite_only, list) or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError("unsupported FIG empty-medium-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from empty-medium-expiry reference")
        image = image_bytes(original)
        checks = (
            ("item_command_image_offset", "item_command_machine_code", 0x0a84,
             "8b1e3a31d1e383bf232f017506e8a406e9aa01"),
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code",
             0x11fc, "e8b91be87801e8d15ce80d2ab80300e82f26"),
            ("item_dispatch_tail_image_offset",
             "item_dispatch_tail_machine_code", 0x1235,
             "e89631d1e68b84bd2bc706a93c0200ffd0c706a93c0100e80f46e8c531"),
            ("medium_af_dismiss_entry_image_offset",
             "medium_af_dismiss_entry_machine_code", 0x4886,
             "c7069b310200c706b92b4400eba6"),
            ("medium_dismiss_handler_image_offset",
             "medium_dismiss_handler_machine_code", 0x482e,
             "c7069b310000c706b92b4a00b83d00e8f7128b1e9b3183bfa531507207b80500e8ecefc3e8c0f38b1e9b31c787a5315000a1b92ba32543c70627430100c7062343ae00e87d1dc706a93c0100b90800e85626e8ff16e2f8c3"),
            ("player_expiry_dispatch_image_offset",
             "player_expiry_dispatch_machine_code", 0x0c41,
             "8b1e3a31d1e383bf3c3100741bff8f3c3183bf3c310075108b36dd318b445f89445dbe372de83e01"),
            ("expiry_card_image_offset", "expiry_card_machine_code", 0x0da7,
             "53c7061f35040056e80620e8c505a125432d08003d64007203b80000a31b35c7061d357800"),
            ("cleanup_tail_image_offset", "cleanup_tail_machine_code", 0x0d98,
             "e81d20e83861b80500e8992ae968fb"),
        )
        for offset_key, code_key, offset, code in checks:
            if expected.get(offset_key) != offset or \
                    expected.get(code_key) != code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    "FIG empty-medium-expiry instruction differs: " + code_key)

        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError("ITEM.EXE differs from empty-medium-expiry reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG empty-medium-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG empty-medium-expiry input evidence differs")
        if (expected.get("capture_wait_seconds"),
                expected.get("capture_pace_seconds"),
                expected.get("capture_time_limit_seconds"),
                expected.get("capture_review_fps"),
                expected.get("capture_review_frames"),
                expected.get("capture_video_frames"),
                expected.get("capture_dosbox_exit_code")) != \
                (5, 5, 80, 70, 5599, 5606, 0) or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG empty-medium-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Twenty-eight stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "4886", "482e", "0c41", "0da7",
                    "0d98")):
            raise ValueError("FIG empty-medium-expiry limitation is missing")

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
                    expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG empty-medium-expiry rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG empty-medium-expiry missed replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (121, "item_pose_at_milliseconds"),
            (126, "darkest_pose_at_milliseconds"),
            (127, "first_restoration_at_milliseconds"),
            (131, "restored_pose_at_milliseconds"),
            (132, "expiry_at_milliseconds"),
            (133, "clean_at_milliseconds"),
            (134, "monster_attack_at_milliseconds"),
            (154, "monster_tail_at_milliseconds"),
            (155, "round_boundary_at_milliseconds"),
            (156, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                times[127] - expected["dismiss_voice_at_milliseconds"] != \
                    expected["empty_slot_hold_milliseconds"] or \
                times[133] - times[132] != \
                    expected["expiry_hold_milliseconds"] or \
                times[134] - times[133] != \
                    expected["clean_hold_milliseconds"] or \
                times[156] - times[155] != \
                    expected["next_command_at_milliseconds"] - \
                    expected["round_boundary_at_milliseconds"]:
            raise ValueError("FIG empty-medium-expiry timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 9 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (10381, "309e81a048cdcef2") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (12413, "ce3659387971554b"):
            raise ValueError("FIG empty-medium-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG empty-medium-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG empty-medium-expiry exact page differs: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        for page in rewrite_only:
            pixels, palette = frames[page["rewrite_frame"]]
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(expand_rgb(pixels, palette)) != \
                        page["rewrite_rgb_sha256"] or \
                    "original_rgb_sha256" in page:
                raise ValueError(
                    "FIG empty-medium-expiry rewrite-only page differs: " +
                    page["kind"])

        # The empty-slot 4844 path returns after five ticks and the direct
        # item tail restores pose zero. 0c41 must immediately enter 0da7
        # without exposing 0d98 first. Only after the 18-tick expiry card may
        # the ordinary cleanup page appear.
        if changed_box(frames[131][0], frames[132][0]) != (
                expected["changed_from_restored_pose_pixels"],
                expected["changed_from_restored_pose_bbox"]) or \
                changed_box(frames[132][0], frames[133][0]) != (
                    expected["changed_from_expiry_to_clean_pixels"],
                    expected["changed_from_expiry_to_clean_bbox"]) or \
                expected["changed_from_restored_pose_pixels"] != 2568 or \
                expected["changed_from_restored_pose_bbox"] != \
                    [16, 120, 95, 198] or \
                expected["changed_from_expiry_to_clean_pixels"] != 3677 or \
                expected["changed_from_expiry_to_clean_bbox"] != \
                    [16, 120, 95, 199] or \
                frames[131] == frames[132] or frames[132] == frames[133] or \
                frames[133] != frames[155]:
            raise ValueError(
                "FIG empty-medium expiry lost pose or inserted early cleanup")

        print(
            "FIG item empty-medium expiry checkpoint: 28 original RGB pages "
            "lock the empty-slot hold, retained pose zero, same-turn expiry "
            "and the sole following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item empty-medium expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
