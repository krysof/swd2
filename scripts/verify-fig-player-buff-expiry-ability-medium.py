#!/usr/bin/env python3
"""Lock FIG same-turn attack-buff expiry after learned ability 51 installs AE."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "pose_zero", "pose_four", "darkening_step_two",
    "darkening_step_three", "darkening_step_four", "darkest_pose_four",
) + tuple(f"flight_frame{index}" for index in range(1, 20)) + (
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
    "monster_action_tail", "round_boundary", "next_command",
)
EXPECTED_REWRITE = (
    121, 122, 124, 125, 126, 127,
) + tuple(range(129, 148)) + (154, 155, 156) + \
    tuple(range(157, 164)) + tuple(range(166, 176)) + (176, 177, 178)
EXPECTED_ORIGINAL = (
    3908, 3920, 3929, 3933, 3937, 3941,
    3973, 3974, 3976, 3978, 3980, 3982, 3984, 3986, 3988, 3990,
    3992, 3994, 3996, 3998, 4000, 4002, 4004, 4006, 4008,
    4052, 4118, 4141, 4158, 4160, 4162, 4164, 4166, 4168, 4170,
    4174, 4176, 4178, 4180, 4182, 4184, 4186, 4188, 4190, 4194,
    4196, 4228, 4249,
)
EXPECTED_STABLE = (
    3919, 3927, 3931, 3935, 3939, 3972,
    3973, 3975, 3977, 3979, 3981, 3983, 3985, 3987, 3989, 3991,
    3993, 3995, 3997, 3999, 4001, 4003, 4005, 4007, 4009,
    4117, 4136, 4157, 4159, 4161, 4163, 4165, 4167, 4169, 4173,
    4175, 4177, 4179, 4181, 4183, 4185, 4187, 4189, 4193, 4195,
    4227, 4248, 5250,
)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "flight_frame0",
    "palette_restoration_step1", "palette_restoration_step2",
    "palette_restoration_step3", "palette_restoration_step4",
    "palette_restoration_step5", "restored_medium_pose",
    "final_monster_shake_clean", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (123, 128, 148, 149, 150, 151, 152, 153, 164, 165)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed learned-medium-install digest {label}")
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
                    "original_fig_player_buff_expiry_after_learned_medium_install" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("medium_ability_id"),
                 expected.get("medium_effect_code")) != (51, 0x31) or \
                (expected.get("medium_slot"),
                 expected.get("medium_sprite_id"),
                 expected.get("expiry_player_turn")) != (0, 0xae, 4) or \
                not isinstance(pages, list) or len(pages) != 48 or \
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
            raise ValueError("unsupported FIG learned-medium-install reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from learned-medium-install reference")
        image = image_bytes(original)
        checks = (
            ("player_command_image_offset", "player_command_machine_code",
             0x0a97, "83bf232f047506e83838e99d01"),
            ("learned_pose_image_offset", "learned_pose_machine_code", 0x4338,
             "e87deae83cd0e8952bb80300e8f6f48306e13104e869eae828d0e8812be8bdf8b80300e8dff4"),
            ("dispatcher_tail_image_offset", "dispatcher_tail_machine_code",
             0x4377, "d1e68b84bd2bc706a93c0200ffd0c706a93c0100e8d014e88600"),
            ("medium_install_handler_image_offset",
             "medium_install_handler_machine_code", 0x4792,
             "c706b92b4a00c706bb2b0100b83100e89313a1e331a32543c7062743a000c7062343ae00c706a93c0100c7069b310000e87c13c3"),
            ("medium_flight_image_offset", "medium_flight_machine_code", 0x5b41,
             'e8d1e0a1b92b2b0625433d50007603b81400ba0000b91400f7f13d000075014050e8c9e0e8d10ae81704e8681358813eb92b9001742e8b0e25438b16b92b3bca771803c83bca730601062543eb1689162543c706b92b9001eb0a2bc83bca76ee29062543813ebb2b9001743a8b0e27438b16bb2b3bca771a83c1083bca73078306274308eb2089162743c706bb2b9001eb1483e9083bca76ed832e274308813e274360ea77e0813eb92b90017403e96fff813ebb2b90017403e964ff8b1e9b31a1234389879d31a125438987a531a127438987ad31c3'),
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
                    "FIG learned-medium-install instruction differs: " + code_key)

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG learned-medium-install fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG learned-medium-install input evidence differs")
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
            raise ValueError("FIG learned-medium-install capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Forty-eight stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a97", "4338", "4377", "4792", "5b41", "0c41",
                    "0da7", "0d98")):
            raise ValueError("FIG learned-medium-install limitation is missing")

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
            raise ValueError("FIG learned-medium-install rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG learned-medium-install missed replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (121, "pose_zero_at_milliseconds"),
            (122, "pose_four_at_milliseconds"),
            (127, "darkest_pose_at_milliseconds"),
            (128, "first_flight_at_milliseconds"),
            (147, "last_flight_at_milliseconds"),
            (153, "restored_medium_at_milliseconds"),
            (154, "expiry_at_milliseconds"),
            (155, "clean_at_milliseconds"),
            (156, "monster_attack_at_milliseconds"),
            (176, "monster_tail_at_milliseconds"),
            (177, "round_boundary_at_milliseconds"),
            (178, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                [times[index + 1] - times[index]
                 for index in range(128, 153)] != \
                    expected["flight_frame_intervals_milliseconds"] or \
                expected.get("flight_frame_milliseconds") != 55 or \
                times[155] - times[154] != \
                    expected["expiry_hold_milliseconds"] or \
                times[156] - times[155] != \
                    expected["clean_hold_milliseconds"] or \
                times[178] - times[177] != \
                    expected["next_command_at_milliseconds"] - \
                    expected["round_boundary_at_milliseconds"]:
            raise ValueError("FIG learned-medium-install timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 9 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (10546, "56054b2c8ee75346") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (13457, "ce3659387971554b"):
            raise ValueError("FIG learned-medium-install voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG learned-medium-install frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG learned-medium-install exact page differs: " +
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
                    "FIG learned-medium-install rewrite-only page differs: " +
                    page["kind"])

        # The restored 4417 page still contains the newly installed AE icon.
        # Returning through the learned dispatcher must enter 0c41 immediately:
        # the expiry compositor clears AE but retains learned pose four, and the
        # sole ordinary clean page follows only after the 18-tick expiry card.
        if changed_box(frames[153][0], frames[154][0]) != (
                expected["changed_from_restored_medium_pixels"],
                expected["changed_from_restored_medium_bbox"]) or \
                changed_box(frames[154][0], frames[155][0]) != (
                    expected["changed_from_expiry_to_clean_pixels"],
                    expected["changed_from_expiry_to_clean_bbox"]) or \
                expected["changed_from_restored_medium_pixels"] != 3795 or \
                expected["changed_from_restored_medium_bbox"] != \
                    [12, 1, 317, 198] or \
                expected["changed_from_expiry_to_clean_pixels"] != 3704 or \
                expected["changed_from_expiry_to_clean_bbox"] != \
                    [12, 120, 91, 199] or \
                frames[153] == frames[154] or frames[154] == frames[155] or \
                frames[155] != frames[177]:
            raise ValueError(
                "FIG learned medium install lost pose or cleaned before expiry")

        print(
            "FIG learned medium-install expiry checkpoint: 48 original RGB "
            "pages lock learned pose four, AE flight, same-turn expiry and "
            "the sole following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG learned medium-install expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
