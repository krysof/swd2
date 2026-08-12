#!/usr/bin/env python3
"""Lock FIG same-turn attack-buff expiry after item 206 installs medium AF."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PRE_KINDS = (
    "item_pose_zero", "darkening_step_two", "darkening_step_three",
    "darkening_step_four", "darkest_item_pose_zero",
) + tuple(f"flight_frame{index}" for index in range(1, 27))
PRE_REWRITE = (120, 122, 123, 124, 125) + tuple(range(127, 153))
PRE_ORIGINAL = (3553, 3563, 3567, 3571, 3575) + (
    3606, 3608, 3610, 3612, 3614, 3616, 3618, 3620, 3622, 3624,
    3626, 3628, 3630, 3632, 3634, 3636, 3638, 3640, 3642, 3644,
    3646, 3648, 3650, 3651, 3653, 3655,
)
PRE_STABLE = (3561, 3565, 3569, 3573, 3604) + (
    3606, 3608, 3610, 3612, 3614, 3616, 3618, 3620, 3623, 3625,
    3627, 3629, 3631, 3633, 3635, 3637, 3639, 3641, 3643, 3645,
    3647, 3649, 3650, 3652, 3654, 3656,
)
POST_KINDS = (
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
    "monster_action_tail", "round_boundary", "next_command",
)
POST_REWRITE = (159, 160, 161) + tuple(range(162, 169)) + \
    tuple(range(171, 181)) + (181, 182, 183)
POST_ORIGINAL = (3698, 3770, 3793, 3811, 3813, 3815, 3817, 3819, 3821,
                 3823, 3827, 3829, 3831, 3833, 3835, 3837, 3839, 3841,
                 3843, 3847, 3849, 3880, 3902)
POST_STABLE = (3769, 3792, 3810, 3812, 3814, 3816, 3818, 3820, 3822,
               3825, 3827, 3829, 3831, 3833, 3835, 3837, 3839, 3841,
               3845, 3848, 3879, 3900, 5250)
EXPECTED_KINDS = PRE_KINDS + POST_KINDS
EXPECTED_REWRITE = PRE_REWRITE + POST_REWRITE
EXPECTED_ORIGINAL = PRE_ORIGINAL + POST_ORIGINAL
EXPECTED_STABLE = PRE_STABLE + POST_STABLE
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "flight_frame0",
    "palette_restoration_step1", "palette_restoration_step2",
    "palette_restoration_step3", "palette_restoration_step4",
    "palette_restoration_step5", "restored_af_medium_pose",
    "final_monster_shake_clean", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (121, 126, 153, 154, 155, 156, 157, 158, 169, 170)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed item-AF-expiry digest {label}")
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
                    "original_fig_player_buff_expiry_after_item_af_install" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"),
                 expected.get("item_effect_code"),
                 expected.get("embedded_ability_id")) != (206, 0x3b, 66) or \
                (expected.get("medium_slot"),
                 expected.get("medium_sprite_id"),
                 expected.get("expiry_player_turn")) != (1, 0xaf, 4) or \
                not isinstance(pages, list) or len(pages) != 54 or \
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
            raise ValueError("unsupported FIG item-AF-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from item-AF-expiry reference")
        image = image_bytes(original)
        checks = (
            ("item_command_image_offset", "item_command_machine_code", 0x0a84,
             "8b1e3a31d1e383bf232f017506e8a406e9aa01"),
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code",
             0x11fc, "e8b91be87801e8d15ce80d2ab80300e82f26"),
            ("item_dispatch_tail_image_offset", "item_dispatch_tail_machine_code",
             0x1235,
             "e89631d1e68b84bd2bc706a93c0200ffd0c706a93c0100e80f46e8c531"),
            ("medium_install_handler_image_offset",
             "medium_install_handler_machine_code", 0x47c6,
             "c706b92b4400c706bb2b0100b83100e85f13a1e331a32543c7062743a000c7062343af00c706a93c0100c7069b310200e84813c3"),
            ("medium_flight_image_offset", "medium_flight_machine_code", 0x5b41,
             "e8d1e0a1b92b2b0625433d50007603b81400ba0000b91400f7f13d000075014050e8c9e0e8d10ae81704e8681358813eb92b9001742e8b0e25438b16b92b3bca771803c83bca730601062543eb1689162543c706b92b9001eb0a2bc83bca76ee29062543813ebb2b9001743a8b0e27438b16bb2b3bca771a83c1083bca73078306274308eb2089162743c706bb2b9001eb1483e9083bca76ed832e274308813e274360ea77e0813eb92b90017403e96fff813ebb2b90017403e964ff8b1e9b31a1234389879d31a125438987a531a127438987ad31c3"),
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
                    f"FIG item-AF-expiry instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG item-AF-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG item-AF-expiry input evidence differs")
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
            raise ValueError("FIG item-AF-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Fifty-four stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "47c6", "5b41", "0c41",
                    "0da7", "0d98")):
            raise ValueError("FIG item-AF-expiry limitation is missing")

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
            raise ValueError("FIG item-AF-expiry rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG item-AF-expiry did not reach replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (120, "item_pose_at_milliseconds"),
            (125, "darkest_item_pose_at_milliseconds"),
            (126, "first_flight_at_milliseconds"),
            (152, "last_flight_at_milliseconds"),
            (158, "restored_medium_at_milliseconds"),
            (159, "expiry_at_milliseconds"),
            (160, "clean_at_milliseconds"),
            (161, "monster_attack_at_milliseconds"),
            (181, "monster_tail_at_milliseconds"),
            (182, "round_boundary_at_milliseconds"),
            (183, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                [times[index + 1] - times[index]
                 for index in range(126, 158)] != \
                    expected["flight_frame_intervals_milliseconds"] or \
                expected.get("flight_frame_milliseconds") != 55 or \
                times[160] - times[159] != \
                    expected["expiry_hold_milliseconds"] or \
                times[161] - times[160] != \
                    expected["clean_hold_milliseconds"] or \
                times[183] - times[182] != \
                    expected["next_command_at_milliseconds"] - \
                    expected["round_boundary_at_milliseconds"]:
            raise ValueError("FIG item-AF-expiry event timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 9 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (10381, "56054b2c8ee75346") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (13676, "ce3659387971554b"):
            raise ValueError("FIG item-AF-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG item-AF-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG item-AF-expiry exact page differs: " +
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
                    "FIG item-AF-expiry rewrite-only page differs: " +
                    page["kind"])

        # 4417 restores the final AF page and returns straight to the item
        # caller's 0c41 expiry dispatch. There must be no bare 0d98 cleanup
        # page or five-tick hold between those adjacent modern frames.
        if changed_box(frames[158][0], frames[159][0]) != (
                expected["changed_from_restored_medium_pixels"],
                expected["changed_from_restored_medium_bbox"]) or \
                changed_box(frames[159][0], frames[160][0]) != (
                    expected["changed_from_expiry_to_clean_pixels"],
                    expected["changed_from_expiry_to_clean_bbox"]) or \
                expected["changed_from_restored_medium_pixels"] != 3798 or \
                expected["changed_from_restored_medium_bbox"] != \
                    [16, 1, 293, 198] or \
                expected["changed_from_expiry_to_clean_pixels"] != 3677 or \
                expected["changed_from_expiry_to_clean_bbox"] != \
                    [16, 120, 95, 199] or \
                frames[158] == frames[159] or frames[159] == frames[160] or \
                frames[160] != frames[182]:
            raise ValueError(
                "FIG item AF expiry inserted a pre-expiry clean page")

        print(
            "FIG item-AF expiry checkpoint: 54 original RGB pages lock "
            "the AF flight and post-expiry battle; FIG.EXE ordering and "
            "the rewrite timeline defer cleanup until after same-turn expiry")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item-AF expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
