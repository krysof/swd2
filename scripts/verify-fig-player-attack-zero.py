#!/usr/bin/env python3
"""Replay and lock FIG's zero player-physical-attack presentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "initial_command_page", "attack_command_selected",
    "pose0", "pose1", "pose2",
    *(f"weapon_wipe{index}" for index in range(1, 5)),
    *(f"reaction_wipe{index}" for index in range(1, 5)),
    *(f"zero_result_rise{index}" for index in range(1, 11)),
    "zero_result_commit_retains_pose2", "player_round_bare_boundary",
    "monster_action_card", "monster_pre_shake_clean",
    *(f"monster_shake_step{index}" for index in range(1, 7)),
    "monster_shake_step8",
    *(f"monster_damage_rise{index}" for index in range(1, 11)),
    "monster_round_bare_boundary", "next_command_page",
)
EXPECTED_REWRITE_FRAMES = (
    *range(1, 34), 35, *range(36, 48),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed FIG player-zero digest {label}")
    return value


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
                expected.get("kind") != "original_fig_player_attack_zero" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("monster_definition_id") != 500 or \
                expected.get("monster_physical_immunity") != 1 or \
                expected.get("monster_evasion") != 0 or \
                expected.get("critical_countdown") != 0xFFFF or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES:
            raise ValueError("unsupported FIG player-zero reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from player-zero reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["fixture_item_sha256"]:
            raise ValueError("FIG player-zero staged game differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG player-zero input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 12 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 839 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG player-zero capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "modern frame 34" not in limitation:
            raise ValueError("FIG player-zero capture limitation missing")

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
                trace.get("boundaries") != expected["rewrite_boundaries"]:
            raise ValueError("FIG player-zero replay boundaries differ")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG player-zero audio/video timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG player-zero final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG player-zero replay did not use IF entry")

        timeline = trace.get("timeline", [])
        frame_times = tuple(
            item["at_milliseconds"] for item in timeline
            if item.get("kind") == "frame")
        if frame_times != tuple(expected["rewrite_frame_times_milliseconds"]):
            raise ValueError("FIG player-zero frame timing differs")
        voices = tuple((
            item.get("call"), item.get("at_milliseconds"),
            item.get("payload_fnv1a64")) for item in timeline
            if item.get("kind") == "voice")
        if voices != (
                (0, expected["identity_voice_at_milliseconds"],
                 expected["identity_voice_payload_fnv1a64"]),
                (1, expected["failure_voice_at_milliseconds"],
                 expected["failure_voice_payload_fnv1a64"]),
                (2, expected["monster_voice_at_milliseconds"],
                 expected["monster_voice_payload_fnv1a64"])):
            raise ValueError("FIG player-zero SP003/K1/SP106 timeline differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG player-zero frame count differs")
        previous_original = -1
        for page in pages:
            if page["original_review_frame"] <= previous_original:
                raise ValueError("FIG player-zero original page order differs")
            previous_original = page["original_review_frame"]
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG player-zero page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        print(
            "FIG player-zero checkpoint: physical poses/wipes, K1, ten red "
            "zero pages, pose-2 commit, bare boundary, complete monster turn "
            "and next command match 46 ordered original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG player-zero checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
