#!/usr/bin/env python3
"""Replay FIG's nonfatal player attack followed by successful monster ability 117 status and its immediate repeat."""

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
    "reaction_wipe1",
    *(f"player_damage_rise{index}" for index in range(1, 11)),
    "player_result_background_clear",
    "player_result_commit_retains_pose2",
    "monster_special_status_prepare_bare",
    "monster_special_status_actor_page",
    "monster_special_status_name_and_icon",
    "monster_special_status_cleanup_bare",
    "next_round_bare_battlefield", "second_action_prepare_bare",
    "second_action_status_actor_page", "second_action_name_and_icon",
)
EXPECTED_REWRITE_FRAMES = (
    *range(1, 11), *range(14, 27), *range(28, 35),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(
            f"malformed FIG player-attack-special-status digest {label}")
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
                expected.get("kind") != \
                    "original_fig_player_attack_special_status" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("monster_definition_id") != 500 or \
                expected.get("monster_hit_points") != 60000 or \
                expected.get("monster_physical_immunity") != 0 or \
                expected.get("monster_evasion") != 0 or \
                expected.get("monster_ai_type") != 0 or \
                expected.get("monster_primary_chance") != 9 or \
                expected.get("monster_secondary_chance") != 9 or \
                expected.get("monster_special_ability_a") != 117 or \
                expected.get("monster_special_ability_b") != 117 or \
                expected.get("ability_id") != 117 or \
                expected.get("effect_code") != 0x64 or \
                expected.get("actor_status_immunity") != 0 or \
                expected.get("critical_countdown") != 0xFFFF or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES:
            raise ValueError(
                "unsupported FIG player-attack-special-status reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError(
                "FIG.EXE differs from player-attack-special-status reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["fixture_item_sha256"]:
            raise ValueError("FIG player-attack-special-status staged game differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError(
                "FIG player-attack-special-status input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 12 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 839 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError(
                "FIG player-attack-special-status capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "modern frames 11..13" not in limitation or \
                "modern frame 27" not in limitation or \
                expected.get("status_voice_times_milliseconds") != \
                    [1538, 2472]:
            raise ValueError(
                "FIG player-attack-special-status capture limitation missing")
        status_voice = expected.get("status_voice_payload_fnv1a64")
        if not isinstance(status_voice, str) or len(status_voice) != 16 or \
                any(character not in "0123456789abcdef"
                    for character in status_voice):
            raise ValueError("malformed FIG player special-status voice digest")

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
            raise ValueError(
                "FIG player-attack-special-status replay boundaries differ")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError(
                "FIG player-attack-special-status audio/video timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError(
                "FIG player-attack-special-status final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError(
                "FIG player-attack-special-status replay did not use IF entry")

        timeline = trace.get("timeline", [])
        frame_times = tuple(
            item["at_milliseconds"] for item in timeline
            if item.get("kind") == "frame")
        if frame_times != tuple(expected["rewrite_frame_times_milliseconds"]):
            raise ValueError(
                "FIG player-attack-special-status frame timing differs")
        voices = tuple((
            item.get("call"), item.get("at_milliseconds"),
            item.get("payload_fnv1a64")) for item in timeline
            if item.get("kind") == "voice")
        if voices != (
                (0, expected["identity_voice_at_milliseconds"],
                 expected["identity_voice_payload_fnv1a64"]),
                (1, expected["status_voice_times_milliseconds"][0],
                 expected["status_voice_payload_fnv1a64"]),
                (2, expected["status_voice_times_milliseconds"][1],
                 expected["status_voice_payload_fnv1a64"])):
            raise ValueError(
                "FIG player-attack-special-status voice timeline differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError(
                "FIG player-attack-special-status frame count differs")
        previous_original = -1
        for page in pages:
            if page["original_review_frame"] <= previous_original:
                raise ValueError(
                    "FIG player-attack-special-status original page order differs")
            previous_original = page["original_review_frame"]
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG player-attack-special-status page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        print(
            "FIG player-attack-special-status checkpoint: physical poses/wipes, "
            "ten player damage pages, bare-background/pose-2 commit, 20e7 "
            "preparation, selected actor/name/status pages, 22e0 cleanup and "
            "the immediate repeated status action match 30 original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG player-attack-special-status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
