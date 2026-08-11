#!/usr/bin/env python3
"""Lock FIG same-turn attack-buff expiry after item 207 dismisses AF."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "item_pose_zero_with_af_medium", "darkening_step_two", "darkening_step_three",
    "darkening_step_four", "darkest_item_pose_zero",
    "dismissal_flip1", "dismissal_flip3", "dismissal_flip7",
    "restoration_step_two", "restoration_step_three",
    "restoration_step_four", "restored_pose_zero_with_af_medium",
    "attack_buff_expired_without_medium", "expiry_tail_clean", "monster_attack_card",
) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
        "monster_action_tail", "round_boundary", "next_command",
    )
EXPECTED_REWRITE = (
    186, 188, 189, 190, 191, 193, 195, 199, 201, 202, 203, 204,
    205, 206, 207,
) + tuple(range(208, 215)) + tuple(range(217, 227)) + (227, 228, 229)
EXPECTED_ORIGINAL = (
    6296, 6306, 6310, 6314, 6318, 6351, 6355, 6359, 6363, 6365,
    6367, 6369, 6402, 6475, 6498, 6516, 6518, 6520, 6522, 6524,
    6526, 6528, 6532, 6534, 6536, 6538, 6540, 6542, 6544, 6546,
    6548, 6552, 6553, 6585, 6606,
)
EXPECTED_STABLE = (
    6304, 6308, 6312, 6316, 6348, 6352, 6356, 6359, 6363, 6365,
    6367, 6401, 6474, 6497, 6515, 6517, 6519, 6521, 6523, 6525,
    6527, 6530, 6532, 6534, 6536, 6538, 6540, 6542, 6544, 6546,
    6550, 6552, 6584, 6605, 8400,
)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "dismissal_flip0_zmbv_artifact",
    "dismissal_flip2_zmbv_artifact", "dismissal_flip4_zmbv_artifact",
    "dismissal_flip6_zmbv_artifact", "first_restoration_step", "final_shake_alternate",
    "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (187, 192, 194, 196, 198, 200, 215, 216)


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
                    "original_fig_player_buff_expiry_after_item_af_dismissal" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x100c, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("install_item_id"),
                 expected.get("install_effect_code"),
                 expected.get("install_embedded_ability_id")) != \
                    (206, 0x3b, 66) or \
                (expected.get("dismiss_item_id"),
                 expected.get("dismiss_effect_code"),
                 expected.get("dismiss_embedded_ability_id")) != \
                    (207, 0x3e, 67) or \
                expected.get("medium_initially_present") is not False or \
                expected.get("medium_present_before_dismissal") is not True or \
                (expected.get("medium_slot"),
                 expected.get("medium_sprite_id"),
                 expected.get("expiry_player_turn")) != (1, 0xaf, 5) or \
                not isinstance(pages, list) or len(pages) != 35 or \
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
            ("medium_af_install_handler_image_offset",
             "medium_af_install_handler_machine_code", 0x47c6,
             "c706b92b4400c706bb2b0100b83100e85f13a1e331a32543c7062743a000c7062343af00c706a93c0100c7069b310200e84813c3"),
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
                (5, 7, 120, 70, 8400, 8410, 0) or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG empty-medium-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Thirty-five stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "47c6", "4886", "482e",
                    "0c41", "0da7", "0d98")):
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
            (186, "item_pose_at_milliseconds"),
            (191, "darkest_pose_at_milliseconds"),
            (192, "first_dismiss_flip_at_milliseconds"),
            (199, "last_dismiss_flip_at_milliseconds"),
            (200, "first_restoration_at_milliseconds"),
            (204, "restored_pose_at_milliseconds"),
            (205, "expiry_at_milliseconds"),
            (206, "clean_at_milliseconds"),
            (207, "monster_attack_at_milliseconds"),
            (227, "monster_tail_at_milliseconds"),
            (228, "round_boundary_at_milliseconds"),
            (229, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                expected.get("dismiss_voice_at_milliseconds") != 14683 or \
                any(times[index + 1] - times[index] != 55
                    for index in range(192, 204)) or \
                expected.get("dismiss_flip_milliseconds") != 55 or \
                times[205] - times[204] != 55 or \
                times[206] - times[205] != 989 or \
                expected.get("expiry_hold_milliseconds") != 989 or \
                times[207] - times[206] != 275 or \
                expected.get("clean_hold_milliseconds") != 275 or \
                times[229] - times[228] != 110:
            raise ValueError("FIG item-medium-dismiss expiry timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 11 or \
                (voices[9].get("at_milliseconds"),
                 voices[9].get("payload_fnv1a64")) != \
                    (14683, "309e81a048cdcef2") or \
                (voices[10].get("at_milliseconds"),
                 voices[10].get("payload_fnv1a64")) != \
                    (16882, "ce3659387971554b"):
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

        # The populated-slot path completes all eight medium flips and the
        # direct-item tail restores pose zero. 0c41 must immediately enter
        # 0da7 without exposing 0d98 first. Only after the 18-tick expiry card
        # may the ordinary cleanup page appear.
        if changed_box(frames[204][0], frames[205][0]) != (
                expected["changed_from_restored_pose_pixels"],
                expected["changed_from_restored_pose_bbox"]) or \
                changed_box(frames[205][0], frames[206][0]) != (
                    expected["changed_from_expiry_to_clean_pixels"],
                    expected["changed_from_expiry_to_clean_bbox"]) or \
                expected["changed_from_restored_pose_pixels"] != 3484 or \
                expected["changed_from_restored_pose_bbox"] != \
                    [16, 1, 293, 151] or \
                expected["changed_from_expiry_to_clean_pixels"] != 3677 or \
                expected["changed_from_expiry_to_clean_bbox"] != \
                    [16, 120, 95, 199] or \
                frames[204] == frames[205] or frames[205] == frames[206] or \
                frames[206] != frames[228]:
            raise ValueError(
                "FIG item medium-dismiss expiry lost pose or inserted cleanup")

        print(
            "FIG item medium-dismiss expiry checkpoint: 35 original RGB "
            "pages lock AF dismissal, retained pose zero, "
            "same-turn expiry and the sole following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item medium-dismiss expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
