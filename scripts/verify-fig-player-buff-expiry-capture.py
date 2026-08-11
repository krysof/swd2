#!/usr/bin/env python3
"""Lock FIG player attack-buff expiry after a failed capture."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "capture_pose_zero", "capture_action_card", "capture_failed",
    "attack_buff_expired", "expiry_tail_clean", "monster_attack_card",
    "monster_tail_clean", "next_command",
)
EXPECTED_REWRITE_FRAMES = (121, 122, 123, 124, 125, 126, 147, 148)
EXPECTED_ORIGINAL_FRAMES = (6707, 6710, 6744, 6757, 6794, 6815, 6898, 6920)
EXPECTED_STABLE_THROUGH = (6709, 6743, 6756, 6793, 6814, 6829, 6918, 8049)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed capture-expiry digest {label}")
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
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_player_buff_expiry_after_capture_failure" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 0 or \
                expected.get("random_directory_base") != 100 or \
                expected.get("redirected_formation_directory_offset") != 288 or \
                expected.get("random_buffer_offset") != 0x1020 or \
                expected.get("random_cursor") != 20 or \
                expected.get("buff_ability_id") != 38 or \
                expected.get("buff_effect_code") != 0x43 or \
                expected.get("expiry_player_turn") != 4 or \
                expected.get("finishing_action") != "failed capture" or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL_FRAMES or \
                tuple(page.get("original_stable_through") for page in pages) != \
                    EXPECTED_STABLE_THROUGH:
            raise ValueError("unsupported FIG capture-expiry reference")

        original_executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(original_executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the capture-expiry reference")
        image = image_bytes(original_executable)
        for offset_key, code_key, wanted_offset, wanted_code in (
                ("capture_dispatch_image_offset",
                 "capture_dispatch_machine_code", 0x0aa4,
                 "83bf232f037506e83f03e99001"),
                ("capture_failure_tail_image_offset",
                 "capture_failure_tail_machine_code", 0x0e7d,
                 "b80500e8ba29c3"),
                ("expiry_wait_image_offset", "expiry_wait_machine_code",
                 0x0de5, "b81200e8522a")):
            offset = expected.get(offset_key)
            code = expected.get(code_key)
            if offset != wanted_offset or code != wanted_code or \
                    image[offset:offset + len(bytes.fromhex(code))].hex() != code:
                raise ValueError(f"FIG capture-expiry instruction differs: {code_key}")

        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"] or \
                sha256((args.game / "ORC.EXE").read_bytes()) != \
                expected["fixture_orc_sha256"]:
            raise ValueError("FIG capture-expiry fixture differs")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG capture-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 9 or \
                expected.get("capture_time_limit_seconds") != 115 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 8049 or \
                expected.get("capture_video_frames") != 8059 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG capture-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "post-DAC RGB" not in limitation or \
                "6757..6793" not in limitation or \
                "18 INT 08h ticks" not in limitation or \
                "0aa4" not in limitation or "0e7d" not in limitation or \
                "0c41" not in limitation:
            raise ValueError("FIG capture-expiry capture limitation is missing")

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
            raise ValueError("FIG capture-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG capture-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG capture-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        pose_zero = expected["capture_pose_rewrite_frame"]
        capture_card = expected["capture_card_rewrite_frame"]
        failure = expected["failure_rewrite_frame"]
        expiry = expected["expiry_rewrite_frame"]
        clean = expected["clean_rewrite_frame"]
        monster_attack = expected["monster_attack_rewrite_frame"]
        next_command = expected["next_command_rewrite_frame"]
        if (pose_zero, capture_card, failure, expiry, clean, monster_attack,
                next_command) != (121, 122, 123, 124, 125, 126, 148) or \
                (capture_card, failure, expiry, clean, monster_attack) != \
                    (pose_zero + 1, pose_zero + 2, pose_zero + 3,
                     pose_zero + 4, pose_zero + 5) or \
                frame_times[capture_card] != \
                    expected["capture_card_at_milliseconds"] or \
                frame_times[failure] != expected["failure_at_milliseconds"] or \
                frame_times[expiry] != expected["expiry_at_milliseconds"] or \
                frame_times[clean] != expected["clean_at_milliseconds"] or \
                frame_times[monster_attack] != \
                    expected["monster_attack_at_milliseconds"] or \
                frame_times[next_command] != \
                    expected["next_command_at_milliseconds"] or \
                frame_times[failure] - frame_times[capture_card] != 989 or \
                frame_times[expiry] - frame_times[failure] != 275 or \
                frame_times[clean] - frame_times[expiry] != \
                    expected["expiry_hold_milliseconds"] or \
                expected["expiry_hold_milliseconds"] != 989:
            raise ValueError("FIG capture-expiry event timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG capture-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG capture-expiry page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        # 0ded leaves 31dd on fighter pose zero and 31e3 at the selected
        # monster. 0da7 overlays only its four-column-by-32-line card there;
        # inserting 0d98 early or rebuilding 2bb5's portrait changes pixels
        # outside this exact rectangle.
        retained_pixels = frames[pose_zero][0]
        expiry_pixels = frames[expiry][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(retained_pixels, expiry_pixels)) if left != right
        ]
        if len(changed) != 2550 or (
                min(x for x, _ in changed), min(y for _, y in changed),
                max(x for x, _ in changed), max(y for _, y in changed)) != \
                (116, 120, 195, 151):
            raise ValueError("FIG capture expiry lost target-anchored pose-zero page")
        if frames[clean] != frames[147] or \
                frames[expiry] == frames[clean] or \
                frames[failure] == frames[expiry]:
            raise ValueError("FIG capture-expiry cleanup ordering differs")

        print(
            "FIG capture-expiry checkpoint: failed capture retains target-"
            "anchored pose zero, shows attack-buff expiry for 18 ticks, cleans "
            "once, then matches the original monster action and next command "
            "across eight RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG capture-expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
