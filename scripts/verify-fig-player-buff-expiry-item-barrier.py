#!/usr/bin/env python3
"""Lock FIG attack-buff expiry after direct item 204 raises a barrier."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "item_pose_zero", "darkening_step_two", "darkening_step_three",
    "darkening_step_four", "darkest_item_pose_zero", "barrier_target_card",
    "sp338_frame0", "sp338_frame1", "sp338_frame2", "sp338_frame3",
)
EXPECTED_REWRITE_FRAMES = (
    121, 123, 124, 125, 126, 127, 128, 129, 130, 131,
)
EXPECTED_ORIGINAL_FRAMES = (
    3900, 3910, 3914, 3918, 3922, 3952, 3958, 3959, 3961, 3965,
)
EXPECTED_STABLE_THROUGH = (
    3908, 3912, 3916, 3920, 3951, 3957, 3958, 3960, 3964, 3975,
)
REWRITE_ONLY_KINDS = (
    "restored_sp338_frame3", "attack_buff_expired", "expiry_tail_clean",
    "monster_attack_card", "monster_tail_clean", "next_command",
)
REWRITE_ONLY_FRAMES = (136, 137, 138, 139, 160, 161)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item-barrier-expiry digest {label}")
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
                    "original_fig_player_buff_expiry_after_item_barrier" or \
                expected.get("status") != "partial_exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                (expected.get("buff_ability_id"),
                 expected.get("buff_effect_code")) != (38, 0x43) or \
                (expected.get("item_id"), expected.get("item_effect_code")) != \
                    (204, 0x62) or \
                expected.get("expiry_player_turn") != 4 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in pages) != \
                    EXPECTED_STABLE_THROUGH or \
                not isinstance(rewrite_only, list) or \
                tuple(page.get("kind") for page in rewrite_only) != \
                    REWRITE_ONLY_KINDS or \
                tuple(page.get("rewrite_frame") for page in rewrite_only) != \
                    REWRITE_ONLY_FRAMES:
            raise ValueError("unsupported FIG item-barrier-expiry reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from item-barrier-expiry reference")
        image = image_bytes(original_executable)
        instruction_checks = (
            ("item_command_image_offset", "item_command_machine_code", 0x0a84,
             "8b1e3a31d1e383bf232f017506e8a406e9aa01"),
            ("direct_item_pose_image_offset", "direct_item_pose_machine_code",
             0x11fc, "e8b91be87801e8d15ce80d2ab80300e82f26"),
            ("item_dispatch_tail_image_offset", "item_dispatch_tail_machine_code",
             0x1235,
             "e89631d1e68b84bd2bc706a93c0200ffd0c706a93c0100e80f46e8c531"),
            ("barrier_handler_image_offset", "barrier_handler_machine_code",
             0x55fd,
             "e83400ff876431b86200e82d05e821e6e83c06e8c318e8ffe58306254302c70627438200b85201e8ec04b90400e894f3b80400e80ae2c3"),
            ("player_expiry_dispatch_image_offset",
             "player_expiry_dispatch_machine_code", 0x0c41,
             "8b1e3a31d1e383bf3c3100741bff8f3c3183bf3c310075108b36dd318b445f89445dbe372de83e01"),
            ("expiry_wait_image_offset", "expiry_wait_machine_code",
             0x0de5, "b81200e8522a"),
            ("cleanup_tail_image_offset", "cleanup_tail_machine_code",
             0x0d98, "e81d20e83861b80500e8992ae968fb"),
        )
        for offset_key, code_key, wanted_offset, wanted_code in instruction_checks:
            offset = expected.get(offset_key)
            code = expected.get(code_key)
            if offset != wanted_offset or code != wanted_code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(
                    f"FIG item-barrier-expiry instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG item-barrier-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG item-barrier-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 5 or \
                expected.get("capture_time_limit_seconds") != 75 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 5250 or \
                expected.get("capture_video_frames") != 5256 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG item-barrier-expiry capture boundary differs")
        for name in (
                "capture_video_sha256", "capture_manifest_sha256",
                "first_unclaimed_original_png_sha256",
                "first_unclaimed_original_rgb_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "Ten stable" not in limitation or \
                "globally corrupted" not in limitation or \
                "not claimed" not in limitation or \
                expected.get("first_unclaimed_original_review_frame") != 3976 or \
                not all(token in limitation for token in (
                    "0a84", "11fc", "1235", "55fd", "0c41", "0de5",
                    "0d98")):
            raise ValueError("FIG item-barrier-expiry limitation is missing")

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
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG item-barrier-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG item-barrier-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG item-barrier-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        timed_frames = (
            (121, "item_pose_at_milliseconds"),
            (126, "darkest_pose_at_milliseconds"),
            (127, "barrier_target_at_milliseconds"),
            (131, "barrier_last_at_milliseconds"),
            (136, "restored_barrier_at_milliseconds"),
            (137, "expiry_at_milliseconds"),
            (138, "clean_at_milliseconds"),
            (139, "monster_attack_at_milliseconds"),
            (160, "monster_tail_clean_at_milliseconds"),
            (161, "next_command_at_milliseconds"),
        )
        if any(frame_times[index] != expected[key]
               for index, key in timed_frames) or \
                frame_times[132] - frame_times[131] != 275 or \
                expected.get("barrier_last_hold_milliseconds") != 275 or \
                frame_times[137] - frame_times[136] != 55 or \
                frame_times[138] - frame_times[137] != 989 or \
                expected.get("expiry_hold_milliseconds") != 989 or \
                frame_times[139] - frame_times[138] != 275 or \
                frame_times[161] - frame_times[160] != 110:
            raise ValueError("FIG item-barrier-expiry event timing differs")
        voices = [item for item in trace.get("timeline", [])
                  if item.get("kind") == "voice"]
        if voices[7].get("at_milliseconds") != 10118 or \
                voices[7].get("payload_fnv1a64") != "c20417b30bf8d63e" or \
                voices[8].get("at_milliseconds") != 12592 or \
                voices[8].get("payload_fnv1a64") != "ce3659387971554b":
            raise ValueError("FIG item-barrier-expiry voices differ")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG item-barrier-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG item-barrier-expiry exact page differs: " +
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
                    "FIG item-barrier-expiry rewrite-only page differs: " +
                    page["kind"])

        # The captured original locks every stable page through SP338. The
        # unmodified call/jump sequence proves that 4417 then returns directly
        # to 0c41, where 0da7 runs before 0d98. The modern trace must therefore
        # place the expiry immediately after the restored barrier page. 0da7
        # rebuilds the item pose and changes exactly its 80x32 card, with no
        # inserted bare page.
        item_pixels = frames[121][0]
        restored_pixels = frames[136][0]
        expiry_pixels = frames[137][0]
        changed_from_item = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(item_pixels, expiry_pixels)) if left != right
        ]
        changed_from_restored = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(restored_pixels, expiry_pixels)) if left != right
        ]
        item_bbox = (
            min(x for x, _ in changed_from_item),
            min(y for _, y in changed_from_item),
            max(x for x, _ in changed_from_item),
            max(y for _, y in changed_from_item))
        restored_bbox = (
            min(x for x, _ in changed_from_restored),
            min(y for _, y in changed_from_restored),
            max(x for x, _ in changed_from_restored),
            max(y for _, y in changed_from_restored))
        if len(changed_from_item) != expected["changed_from_item_pose_pixels"] or \
                list(item_bbox) != expected["changed_from_item_pose_bbox"] or \
                len(changed_from_item) != 2560 or \
                item_bbox != (16, 120, 95, 151) or \
                len(changed_from_restored) != \
                    expected["changed_from_restored_barrier_pixels"] or \
                list(restored_bbox) != \
                    expected["changed_from_restored_barrier_bbox"] or \
                len(changed_from_restored) != 4192 or \
                restored_bbox != (16, 120, 95, 195) or \
                frames[137] == frames[138] or frames[138] != frames[160]:
            raise ValueError("FIG item-barrier expiry inserted an early clean page")

        print(
            "FIG item-barrier expiry checkpoint: ten original RGB pages lock "
            "the direct barrier body; FIG.EXE ordering and the rewrite "
            "timeline retain SP338 until the 18-tick expiry and defer the "
            "sole clean page")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item-barrier expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
