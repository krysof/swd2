#!/usr/bin/env python3
"""Lock item 219's two support cards followed by same-turn buff expiry."""

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
    "first_composite_buff_card", "attack_buff_expired",
    "expiry_tail_clean", "monster_attack_card",
) + tuple(f"monster_shake{index}" for index in range(1, 8)) + \
    tuple(f"damage_rise{index}" for index in range(1, 11)) + (
        "monster_action_tail", "round_boundary", "next_command",
    )
EXPECTED_REWRITE = (
    120, 122, 123, 124, 125, 126, 141, 142, 143,
) + tuple(range(144, 151)) + tuple(range(153, 163)) + (163, 164, 165)
EXPECTED_ORIGINAL = (
    3550, 3560, 3564, 3568, 3572, 3602, 3710, 3783, 3806,
    3824, 3827, 3829, 3831, 3833, 3835, 3837,
    3841, 3843, 3846, 3848, 3850, 3852, 3854, 3856, 3860, 3864,
    3866, 3897, 3919,
)
EXPECTED_STABLE = (
    3558, 3562, 3566, 3570, 3599, 3637, 3782, 3805, 3823,
    3826, 3828, 3830, 3832, 3834, 3836, 3840,
    3842, 3844, 3846, 3848, 3850, 3852, 3854, 3858, 3862, 3865,
    3896, 3917, 5250,
)
REWRITE_ONLY_KINDS = (
    "first_darkening_step", "first_buff_return_pose",
) + tuple(f"first_restoration_step{index}" for index in range(1, 6)) + (
    "between_nested_effects", "second_composite_buff_card",
    "second_buff_return_pose",
) + tuple(f"second_restoration_step{index}" for index in range(1, 6)) + (
    "final_shake_alternate", "pre_damage_reaction",
)
REWRITE_ONLY_FRAMES = (121, 127, 128, 129, 130, 131, 132, 133, 134, 135,
                       136, 137, 138, 139, 140, 151, 152)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed composite-support-expiry digest {label}")
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
                    "original_fig_player_buff_expiry_after_item_composite_support" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                (expected.get("formation_directory_offset"),
                 expected.get("random_buffer_offset"),
                 expected.get("random_cursor")) != (392, 0x1020, 20) or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code"),
                 expected.get("embedded_ability_id"),
                 expected.get("nested_effects")) != \
                    (219, 0x6b, 79, [0x66, 0x69]) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or len(pages) != 29 or \
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
            raise ValueError("unsupported FIG composite-support-expiry reference")

        original = (args.game / "FIG.EXE").read_bytes()
        if sha256(original) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from composite-support-expiry reference")
        image = image_bytes(original)
        checks = (
            ("item_command_image_offset", "item_command_machine_code", 0x0a84,
             "8b1e3a31d1e383bf232f017506e8a406e9aa01"),
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code",
             0x11fc, "e8b91be87801e8d15ce80d2ab80300e82f26"),
            ("item_dispatch_tail_image_offset",
             "item_dispatch_tail_machine_code", 0x1235,
             "e89631d1e68b84bd2bc706a93c0200ffd0c706a93c0100e80f46e8c531"),
            ("composite_handler_image_offset", "composite_handler_machine_code",
             0x57f2,
             "be8c0003368d31e800c583c609268b04508ad8b700d1e38b87bd2bc706a93c0200ffd0c706a93c0100588b1e973183bfad33007501c38adcb700d1e38b87bd2bc706a93c0200ffd0c706a93c0100c3"),
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
                    "FIG composite-support-expiry instruction differs: " +
                    code_key)

        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError("ITEM.EXE differs from composite-support-expiry reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                    expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                    expected["fixture_name_sha256"]:
            raise ValueError("FIG composite-support-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG composite-support-expiry inputs differ")
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
            raise ValueError("FIG composite-support-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Twenty-nine stable full 320x200 pages" not in limitation or \
                "deliberately unpaired" not in limitation or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "57f2", "0c41", "0da7",
                    "0d98")):
            raise ValueError("FIG composite-support-expiry limitation is missing")

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
            raise ValueError("FIG composite-support-expiry rewrite trace differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG composite-support-expiry missed replay exit")

        times = [entry["at_milliseconds"] for entry in trace["timeline"]
                 if entry.get("kind") == "frame"]
        timed = (
            (120, "item_pose_at_milliseconds"),
            (125, "darkest_pose_at_milliseconds"),
            (126, "first_buff_card_at_milliseconds"),
            (127, "first_buff_return_at_milliseconds"),
            (133, "between_nested_at_milliseconds"),
            (134, "second_buff_card_at_milliseconds"),
            (135, "second_buff_return_at_milliseconds"),
            (141, "expiry_at_milliseconds"), (142, "clean_at_milliseconds"),
            (143, "monster_attack_at_milliseconds"),
            (163, "monster_tail_at_milliseconds"),
            (164, "round_boundary_at_milliseconds"),
            (165, "next_command_at_milliseconds"),
        )
        if any(times[index] != expected[key] for index, key in timed) or \
                times[127] - times[126] != expected["first_buff_hold_milliseconds"] or \
                times[134] - times[133] != \
                    expected["between_nested_hold_milliseconds"] or \
                times[135] - times[134] != \
                    expected["second_buff_hold_milliseconds"] or \
                times[141] - times[140] != 55 or \
                times[142] - times[141] != expected["expiry_hold_milliseconds"] or \
                times[143] - times[142] != expected["clean_hold_milliseconds"]:
            raise ValueError("FIG composite-support-expiry timing differs")
        voices = [entry for entry in trace["timeline"]
                  if entry.get("kind") == "voice"]
        if len(voices) != 10 or \
                (voices[7].get("at_milliseconds"),
                 voices[7].get("payload_fnv1a64")) != \
                    (expected["first_nested_voice_at_milliseconds"],
                     "1b495fae5a702d12") or \
                (voices[8].get("at_milliseconds"),
                 voices[8].get("payload_fnv1a64")) != \
                    (expected["second_buff_card_at_milliseconds"],
                     "c6ae00bdfaa4b22e") or \
                (voices[9].get("at_milliseconds"),
                 voices[9].get("payload_fnv1a64")) != \
                    (expected["monster_voice_at_milliseconds"],
                     "ce3659387971554b"):
            raise ValueError("FIG composite-support-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG composite-support-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG composite-support-expiry exact page differs: " +
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
                    "FIG composite-support-expiry rewrite-only page differs: " +
                    page["kind"])

        # The nested 66h/69h cards each retain item pose zero.  The first
        # selector exposes one un-delayed bare inter-selector flip.  After the
        # second restoration, 0c41 draws the expiry immediately; the sole
        # *delayed* cleanup follows that 18-tick card and is the same bare page.
        if frames[133] != frames[142] or frames[141] == frames[142] or \
                frames[142] != frames[164]:
            raise ValueError("FIG composite-support-expiry page ownership differs")

        print(
            "FIG composite-support expiry checkpoint: 29 original RGB pages "
            "lock both nested cards, retained item pose, same-turn expiry and "
            "the sole following cleanup")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG composite-support expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
