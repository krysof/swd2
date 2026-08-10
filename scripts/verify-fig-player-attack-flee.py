#!/usr/bin/env python3
"""Replay and lock FIG's nonfatal player attack followed by a monster flee presentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
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
    "monster_flee_card", "post_flee_bare_battlefield",
    "victory_boundary_bare_battlefield", "victory_reward_summary",
)
EXPECTED_REWRITE_FRAMES = (
    *range(1, 11), *range(14, 30),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed FIG player-flee digest {label}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("source_game", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = expected.get("matched_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_player_attack_flee" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_region_map") != 10 or \
                expected.get("monster_definition_id") != 500 or \
                expected.get("monster_hit_points") != 60000 or \
                expected.get("monster_physical_immunity") != 0 or \
                expected.get("monster_evasion") != 0 or \
                expected.get("monster_ai_type") != 2 or \
                expected.get("critical_countdown") != 0xFFFF or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES:
            raise ValueError("unsupported FIG player-flee reference")
        if sha256((args.source_game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from player-flee reference")
        if sha256((args.source_game / "ORC.EXE").read_bytes()) != \
                expected["source_orc_sha256"] or \
                sha256((args.fixture / "ORC.EXE").read_bytes()) != \
                expected["pinned_capture_orc_sha256"]:
            raise ValueError("FIG player-flee source/capture ORC differs")
        if sha256((args.fixture / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.fixture / "ITEM.EXE").read_bytes()) != \
                expected["fixture_item_sha256"]:
            raise ValueError("FIG player-flee staged game differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG player-flee input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 12 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 839 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG player-flee capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "modern frames 11..13" not in limitation:
            raise ValueError("FIG player-flee capture limitation missing")

        args.output.mkdir(parents=True, exist_ok=True)
        modern_game = args.output / "modern-game"
        if modern_game.exists():
            shutil.rmtree(modern_game)
        shutil.copytree(args.source_game, modern_game)
        shutil.copy2(args.fixture / "ITEM.EXE", modern_game / "ITEM.EXE")
        orc_path = modern_game / "ORC.EXE"
        orc = bytearray(orc_path.read_bytes())
        header_size = int.from_bytes(orc[8:10], "little") * 16
        first = bytes(orc[header_size + 100:header_size + 102])
        fixed = bytes(orc[header_size + 392:header_size + 394])
        orc[header_size + 100:header_size + 102] = fixed
        orc[header_size + 392:header_size + 394] = first
        if sha256(orc) != expected["rewrite_orc_sha256"]:
            raise ValueError("FIG player-flee rewrite ORC differs")
        orc_path.write_bytes(orc)

        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(modern_game),
            "--save-dir", str(args.fixture), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"]:
            raise ValueError("FIG player-flee replay boundaries differ")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG player-flee audio/video timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG player-flee final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "OC",
                "launched": True}]:
            raise ValueError("FIG player-flee replay did not use IF entry")

        timeline = trace.get("timeline", [])
        frame_times = tuple(
            item["at_milliseconds"] for item in timeline
            if item.get("kind") == "frame")
        if frame_times != tuple(expected["rewrite_frame_times_milliseconds"]):
            raise ValueError("FIG player-flee frame timing differs")
        voices = tuple((
            item.get("call"), item.get("at_milliseconds"),
            item.get("payload_fnv1a64")) for item in timeline
            if item.get("kind") == "voice")
        if voices != (
                (0, expected["identity_voice_at_milliseconds"],
                 expected["identity_voice_payload_fnv1a64"]),
                (1, expected["flee_voice_at_milliseconds"],
                 expected["flee_voice_payload_fnv1a64"])):
            raise ValueError("FIG player-flee SP003/SV3 timeline differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG player-flee frame count differs")
        previous_original = -1
        for page in pages:
            if page["original_review_frame"] <= previous_original:
                raise ValueError("FIG player-flee original page order differs")
            previous_original = page["original_review_frame"]
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG player-flee page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        print(
            "FIG player-flee checkpoint: physical poses/wipes, ten player "
            "damage pages, bare-background/pose-2 commit, monster flee card, "
            "post-flee/victory bare pages and reward summary match 26 "
            "original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG player-flee checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
