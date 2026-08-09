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
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_level_up_page" or \
                expected.get("formation_directory_offset") != 392 or \
                (expected.get("actor_level_before"),
                 expected.get("actor_level_after")) != (9, 10) or \
                expected.get("rewrite_frame") != 27:
            raise ValueError("unsupported FIG level-up reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the level-up reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original level-up capture input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG level-up staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay",
            str(args.reference.with_name("replay-fig-level-up.txt")),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != {
                "total": 4, "consumed": 4, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 4, "poll": 0, "text": 0, "frontend": 56}:
            raise ValueError("FIG level-up input boundaries differ")
        if trace.get("video") != {
                "frames": 28, "direct_updates": 0,
                "last_width": 320, "last_height": 200,
                "fnv1a64": expected["rewrite_video_fnv1a64"]} or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG level-up video differs")
        if trace.get("audio") != {
                "music_calls": 3, "voice_calls": 1,
                "stop_music_calls": 0, "stop_audio_calls": 1,
                "fnv1a64": "7b969b8fb8024a39"} or \
                trace.get("delay_milliseconds") != 474:
            raise ValueError("FIG level-up audio or delay boundary differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG level-up final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG level-up quit boundary differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 28:
            raise ValueError("FIG level-up frame count differs")
        pixels, palette = frames[expected["rewrite_frame"]]
        rgb = expand_rgb(pixels, palette)
        if sha256(pixels) != expected["rewrite_indexed_sha256"] or \
                sha256(palette) != expected["rewrite_palette_sha256"] or \
                sha256(rgb) != expected["rewrite_rgb_sha256"] or \
                sha256(rgb) != expected["original_rgb_sha256"]:
            raise ValueError("FIG level-up no longer exactly matches original RGB")
        for name in (
                "capture_harness_sha256", "capture_video_sha256",
                "capture_manifest_sha256", "original_png_sha256"):
            if not valid_digest(expected.get(name)):
                raise ValueError(f"malformed level-up evidence digest {name}")
        print(
            "FIG level-up checkpoint: active NAME glyphs, eight right-aligned "
            "old/new values, growth state, WI02, and complete original RGB match")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG level-up checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
