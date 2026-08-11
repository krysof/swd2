#!/usr/bin/env python3
"""Lock FIG same-turn attack-buff expiry after learned B0 dismissal."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "pose_zero_with_b0_medium", "pose_four_with_b0_medium",
    "darkening_step_two", "darkening_step_three", "darkening_step_four",
    "darkest_pose_four", "dismissal_flip1", "dismissal_flip3",
    "dismissal_flip7", "restoration_step_two", "restoration_step_three",
    "restoration_step_four", "restored_pose_four_with_b0_medium",
    "attack_buff_expired_without_medium", "expiry_tail_clean",
    "monster_attack_card", "monster_shake1", "monster_shake2",
    "monster_shake3", "monster_shake4", "monster_shake5",
    "monster_shake6", "monster_shake7",
) + tuple(f"damage_rise{index}" for index in range(1, 11)) + (
    "monster_action_tail", "round_boundary", "next_command",
)
EXPECTED_REWRITE = (
    160, 161, 163, 164, 165, 166, 168, 170, 174, 176, 177, 178,
    179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
) + tuple(range(192, 202)) + (202, 203, 204)
EXPECTED_ORIGINAL = (
    5325, 5337, 5347, 5351, 5354, 5358, 5391, 5395, 5399, 5403,
    5405, 5407, 5409, 5442, 5515, 5538, 5556, 5558, 5560, 5562,
    5564, 5566, 5568, 5572, 5574, 5576, 5578, 5580, 5582, 5584,
    5586, 5588, 5592, 5594, 5625, 5647,
)
EXPECTED_STABLE = (
    5336, 5345, 5349, 5353, 5357, 5388, 5392, 5396, 5399, 5403,
    5405, 5407, 5441, 5514, 5537, 5555, 5557, 5559, 5561, 5563,
    5565, 5567, 5571, 5573, 5575, 5577, 5579, 5581, 5583, 5585,
    5587, 5590, 5593, 5624, 5645, 5668,
)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "dismissal_flip0_zmbv_artifact",
    "dismissal_flip2_zmbv_artifact", "dismissal_flip4_zmbv_artifact",
    "dismissal_flip6_zmbv_artifact", "first_restoration_step",
    "final_shake_alternate", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (162, 167, 169, 171, 173, 175, 190, 191)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed learned-medium-expiry digest {label}")
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
                    "original_fig_player_buff_expiry_after_learned_b0_dismissal" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("install_ability_id"),
                 expected.get("install_effect_code"),
                 expected.get("dismiss_ability_id"),
                 expected.get("dismiss_effect_code")) != \
                    (82, 0x3c, 83, 0x3f) or \
                (expected.get("medium_slot"),
                 expected.get("medium_sprite_id"),
                 expected.get("expiry_player_turn")) != (2, 0xb0, 4) or \
                not isinstance(pages, list) or len(pages) != 36 or \
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
            raise ValueError("unsupported FIG learned-medium-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from learned-medium-expiry reference")
        image = image_bytes(original)
        checks = (
            ("player_command_image_offset", "player_command_machine_code",
             0x0a97, "83bf232f047506e83838e99d01"),
            ("learned_pose_image_offset", "learned_pose_machine_code", 0x4338,
             "e87deae83cd0e8952bb80300e8f6f48306e13104e869eae828d0e8812be8bdf8b80300e8dff4"),
            ("dispatcher_tail_image_offset", "dispatcher_tail_machine_code",
             0x4377, "d1e68b84bd2bc706a93c0200ffd0c706a93c0100e8d014e88600"),
            ("medium_b0_install_handler_image_offset",
             "medium_b0_install_handler_machine_code", 0x47fa,
             "c706b92b3e00c706bb2b0100b83100e82b13a1e331a32543c7062743a000c7062343b000c706a93c0100c7069b310400e81413c3"),
            ("medium_b0_dismiss_entry_image_offset",
             "medium_b0_dismiss_entry_machine_code", 0x4894,
             "c7069b310400c706b92b3e00eb98"),
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
                    "FIG learned-medium-expiry instruction differs: " + code_key)

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG learned-medium-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG learned-medium-expiry input evidence differs")
        if (expected.get("capture_wait_seconds"),
                expected.get("capture_pace_seconds"),
                expected.get("capture_time_limit_seconds"),
                expected.get("capture_review_fps"),
                expected.get("capture_review_frames"),
                expected.get("capture_video_frames"),
                expected.get("capture_dosbox_exit_code")) != \
                (5, 5, 110, 70, 7700, 7709, 0) or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG learned-medium-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Thirty-six stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a97", "4338", "4377", "47fa", "4894", "482e",
                    "0c41", "0da7", "0d98")) or \
                "duplicated final LEFT" not in limitation or \
                "trailing repeated ENTER" not in limitation:
            raise ValueError("FIG learned-medium-expiry limitation is missing")

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
            raise ValueError("FIG learned-medium-expiry rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG learned-medium-expiry missed replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (160, "pose_zero_at_milliseconds"),
            (161, "pose_four_at_milliseconds"),
            (166, "darkest_pose_at_milliseconds"),
            (167, "first_dismissal_flip_at_milliseconds"),
            (174, "last_dismissal_flip_at_milliseconds"),
            (179, "restored_pose_at_milliseconds"),
            (180, "expiry_at_milliseconds"),
            (181, "clean_at_milliseconds"),
            (182, "monster_attack_at_milliseconds"),
            (202, "monster_tail_at_milliseconds"),
            (203, "round_boundary_at_milliseconds"),
            (204, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                any(times[index + 1] - times[index] != 55
                    for index in range(167, 174)) or \
                expected.get("dismissal_flip_milliseconds") != 55 or \
                times[180] - times[179] != 55 or \
                times[181] - times[180] != 989 or \
                expected.get("expiry_hold_milliseconds") != 989 or \
                times[182] - times[181] != 275 or \
                expected.get("clean_hold_milliseconds") != 275 or \
                times[204] - times[203] != 110:
            raise ValueError("FIG learned-medium-expiry timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 9 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (12263, "309e81a048cdcef2") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (14462, "ce3659387971554b"):
            raise ValueError("FIG learned-medium-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG learned-medium-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG learned-medium-expiry exact page differs: " +
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
                    "FIG learned-medium-expiry rewrite-only page differs: " +
                    page["kind"])

        # The last restored 4417 page still contains medium B0. 0c41 must
        # immediately enter 0da7, which recomposes the retained learned pose
        # four after the medium state has been cleared. Only after its 18-tick
        # card may the ordinary 0d98 page appear.
        if changed_box(frames[179][0], frames[180][0]) != (
                expected["changed_from_restored_pose_pixels"],
                expected["changed_from_restored_pose_bbox"]) or \
                changed_box(frames[180][0], frames[181][0]) != (
                    expected["changed_from_expiry_to_clean_pixels"],
                    expected["changed_from_expiry_to_clean_bbox"]) or \
                expected["changed_from_restored_pose_pixels"] != 3484 or \
                expected["changed_from_restored_pose_bbox"] != \
                    [12, 1, 269, 151] or \
                expected["changed_from_expiry_to_clean_pixels"] != 3704 or \
                expected["changed_from_expiry_to_clean_bbox"] != \
                    [12, 120, 91, 199] or \
                frames[179] == frames[180] or frames[180] == frames[181] or \
                frames[181] != frames[203]:
            raise ValueError(
                "FIG learned-medium expiry lost pose or inserted early cleanup")

        print(
            "FIG learned-medium expiry checkpoint: 36 original RGB pages "
            "lock B0 dismissal, retained pose four, same-turn expiry and the "
            "sole following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG learned-medium expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
