#!/usr/bin/env python3
"""Replay and lock FIG directory 42h's special boss hit reaction."""

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
        pages = expected.get("frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_story_42_hit_reaction" or \
                expected.get("formation_directory_offset") != 0x42 or \
                not isinstance(pages, list) or len(pages) != 9 or \
                [p.get("rewrite_frame") for p in pages] != [
                    30, 33, 42, 44, 45, 47, 49, 50, 53]:
            raise ValueError("unsupported FIG story-42 reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the story-42 reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original story-42 capture input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG story-42 staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay",
            str(args.reference.with_name("replay-fig-story-42.txt")),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != {
                "total": 7, "consumed": 7, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 3, "poll": 2, "text": 2, "frontend": 115}:
            raise ValueError("FIG story-42 input boundaries differ")
        if trace.get("video") != {
                "frames": 54, "direct_updates": 4,
                "last_width": 320, "last_height": 200,
                "fnv1a64": expected["rewrite_video_fnv1a64"]} or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG story-42 video differs")
        if trace.get("audio") != {
                "music_calls": 1, "voice_calls": 3,
                "stop_music_calls": 0, "stop_audio_calls": 1,
                "fnv1a64": "d8270c80754cd6a1"} or \
                trace.get("delay_milliseconds") != 1013:
            raise ValueError("FIG story-42 audio or timing differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG story-42 final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG story-42 quit boundary differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 54:
            raise ValueError("FIG story-42 frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG story-42 {page['kind']} no longer matches original")
            if not valid_digest(page.get("original_png_sha256")):
                raise ValueError("malformed story-42 original PNG digest")
        for name in (
                "capture_harness_sha256", "capture_video_sha256",
                "capture_manifest_sha256"):
            if not valid_digest(expected.get(name)):
                raise ValueError(f"malformed story-42 evidence digest {name}")
        print(
            "FIG story-42 checkpoint: two-page prompt, CD523 reaction, "
            "floating damage, command return, complete trace, and nine exact "
            "original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG story-42 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
