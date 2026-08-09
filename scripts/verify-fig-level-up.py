#!/usr/bin/env python3
"""Replay and lock FIG's original one-level growth page."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def valid_digest(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        c in "0123456789abcdef" for c in value)


def verify_case(
        executable: Path, game: Path, save_root: Path, output: Path,
        replay: Path, expected: dict[str, object]) -> None:
    label = expected.get("label")
    if not isinstance(label, str) or not label or \
            expected.get("rewrite_frame") != 27:
        raise ValueError("unsupported FIG level-up case")
    case_save = save_root / str(expected["save_subdirectory"])
    if sha256((case_save / "SAVE.DA1").read_bytes()) != \
            expected["fixture_save_sha256"]:
        raise ValueError(f"FIG {label} staged save differs")

    case_output = output / label
    case_output.mkdir(parents=True, exist_ok=True)
    trace_path = case_output / "trace.json"
    frame_path = case_output / "frames.bin"
    subprocess.run([
        str(executable), "--game", str(game),
        "--save-dir", str(case_save), "--slot", "1", "--no-save",
        "--start-marker", "IF", "--run-replay", str(replay),
        "--trace-output", str(trace_path),
        "--frame-output", str(frame_path),
    ], check=True, stdout=subprocess.DEVNULL)
    trace = json.loads(trace_path.read_text(encoding="utf-8"))
    if trace.get("input") != {
            "total": 4, "consumed": 4, "remaining": 0,
            "implicit_quit_calls": 0} or trace.get("boundaries") != {
                "wait": 4, "poll": 0, "text": 0, "frontend": 56}:
        raise ValueError(f"FIG {label} input boundaries differ")
    if trace.get("video") != {
            "frames": 28, "direct_updates": 0,
            "last_width": 320, "last_height": 200,
            "fnv1a64": expected["rewrite_video_fnv1a64"]} or \
            trace.get("frame_fnv1a64", [])[-1:] != [
                expected["rewrite_final_fnv1a64"]]:
        raise ValueError(f"FIG {label} video differs")
    if trace.get("audio") != {
            "music_calls": 3, "voice_calls": 1,
            "stop_music_calls": 0, "stop_audio_calls": 1,
            "fnv1a64": "7b969b8fb8024a39"} or \
            trace.get("delay_milliseconds") != 474:
        raise ValueError(f"FIG {label} audio or delay boundary differs")
    if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
            trace.get("name_fnv1a64")) != (
                expected["rewrite_state_fnv1a64"],
                "827f0f1b725a0958", "e3d2853e2676513b"):
        raise ValueError(f"FIG {label} final state differs")
    if trace.get("transitions") != [{
            "module": "FIG.EXE", "input": "IF", "output": "--",
            "launched": True}]:
        raise ValueError(f"FIG {label} quit boundary differs")

    frames = load_indexed_frames(frame_path)
    if len(frames) != 28:
        raise ValueError(f"FIG {label} frame count differs")
    pixels, palette = frames[int(expected["rewrite_frame"])]
    rgb = expand_rgb(pixels, palette)
    if sha256(pixels) != expected["rewrite_indexed_sha256"] or \
            sha256(palette) != expected["rewrite_palette_sha256"] or \
            sha256(rgb) != expected["rewrite_rgb_sha256"] or \
            sha256(rgb) != expected["original_rgb_sha256"]:
        raise ValueError(
            f"FIG {label} no longer exactly matches original RGB")
    for name in (
            "capture_video_sha256", "capture_manifest_sha256",
            "original_png_sha256"):
        if not valid_digest(expected.get(name)):
            raise ValueError(f"malformed {label} evidence digest {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        cases = expected.get("cases")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_level_up_pages" or \
                expected.get("formation_directory_offset") != 392 or \
                not isinstance(cases, list) or len(cases) != 2 or \
                [(case.get("actor_level_before"),
                  case.get("actor_level_after"),
                  case.get("learned_ability_id")) for case in cases] != [
                    (9, 10, None), (10, 11, 9)]:
            raise ValueError("unsupported FIG level-up reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the level-up reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original level-up capture input differs")
        args.output.mkdir(parents=True, exist_ok=True)
        if not valid_digest(expected.get("capture_harness_sha256")):
            raise ValueError("malformed level-up capture harness digest")
        replay = args.reference.with_name("replay-fig-level-up.txt")
        for case in cases:
            verify_case(
                args.executable, args.game, args.save_root, args.output,
                replay, case)
        print(
            "FIG level-up checkpoint: active NAME glyphs, eight right-aligned "
            "old/new values, no-ability and learned-ability rows, growth state, "
            "WI02, and two complete original RGB matches")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG level-up checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
