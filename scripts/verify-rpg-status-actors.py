#!/usr/bin/env python3
"""Replay and lock all four actor-specific initial RPG Status pages."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


STATE_TRIPLE = (
    "ea698a5a2cc29c78",
    "827f0f1b725a0958",
    "e3d2853e2676513b",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        actors = reference.get("actors", [])
        if reference.get("schema_version") != 1 or len(actors) != 4:
            raise ValueError("unsupported Status-actor reference")
        args.output.mkdir(parents=True, exist_ok=True)
        for actor, expected in enumerate(actors):
            if expected.get("actor") != actor:
                raise ValueError("Status actors are not in identity order")
            replay = args.reference.parent / f"replay-rpg-status-actor-{actor}.txt"
            trace_path = args.output / f"actor-{actor}-trace.json"
            frame_path = args.output / f"actor-{actor}-frames.bin"
            subprocess.run([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(args.save_dir), "--slot", "1", "--no-save",
                "--run-replay", str(replay),
                "--trace-output", str(trace_path),
                "--frame-output", str(frame_path),
            ], check=True, stdout=subprocess.DEVNULL)

            trace = json.loads(trace_path.read_text(encoding="utf-8"))
            total = expected["rewrite_input_total"]
            if trace.get("input") != {
                    "total": total, "consumed": total, "remaining": 0,
                    "implicit_quit_calls": 0}:
                raise ValueError(f"actor {actor} input accounting differs")
            if trace.get("boundaries") != {
                    "wait": expected["rewrite_wait_boundaries"], "poll": 1,
                    "text": 0, "frontend": 105}:
                raise ValueError(f"actor {actor} input boundaries differ")
            if trace.get("video") != {
                    "frames": expected["rewrite_frames"], "direct_updates": 0,
                    "last_width": 320, "last_height": 200,
                    "fnv1a64": expected["rewrite_video_fnv1a64"]}:
                raise ValueError(f"actor {actor} video summary differs")
            if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                    trace.get("name_fnv1a64")) != STATE_TRIPLE:
                raise ValueError(f"actor {actor} changed the staged save triple")
            if trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
                raise ValueError(f"actor {actor} final frame differs")

            frames = load_indexed_frames(frame_path)
            if len(frames) != expected["rewrite_frames"]:
                raise ValueError(f"actor {actor} capture count differs")
            pixels, palette = frames[-1]
            if hashlib.sha256(pixels).hexdigest() != \
                    expected["rewrite_indexed_sha256"]:
                raise ValueError(f"actor {actor} indexed Status page differs")
            if hashlib.sha256(palette).hexdigest() != \
                    expected["rewrite_palette_sha256"]:
                raise ValueError(f"actor {actor} Status palette differs")
            if hashlib.sha256(expand_rgb(pixels, palette)).hexdigest() != \
                    expected["matched_rgb_sha256"]:
                raise ValueError(f"actor {actor} original RGB page differs")
        print(
            "RPG Status actors checkpoint: four complete indexed/palette "
            "pages and four original 64,000-pixel RGB pages match")
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG Status actors checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
