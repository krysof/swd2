#!/usr/bin/env python3
"""Replay and lock all original RPG equipment rejection boundaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


MAPZ_FNV = "827f0f1b725a0958"
NAME_FNV = "e3d2853e2676513b"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgb_crop(rgb: bytes, box: list[int]) -> bytes:
    if len(box) != 4:
        raise ValueError("equipment-error RGB box must contain four coordinates")
    left, top, right, bottom = box
    if not (0 <= left < right <= 320 and 0 <= top < bottom <= 200):
        raise ValueError("equipment-error RGB box is outside 320x200")
    return b"".join(
        rgb[y * 320 * 3 + left * 3:y * 320 * 3 + right * 3]
        for y in range(top, bottom)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        cases = reference.get("cases", [])
        if reference.get("schema_version") != 1 or len(cases) != 3:
            raise ValueError("unsupported equipment-error reference")
        if reference.get("reference_program_sha256") != \
                "f742990149c72872066d7c95adfdfa53b6366f8a8bee2724ee1494a6af6d558f":
            raise ValueError("equipment-error reference does not identify RPG.EXE")
        args.output.mkdir(parents=True, exist_ok=True)

        expected_ids = ["character", "slot", "twohand"]
        for expected_id, expected in zip(expected_ids, cases):
            if expected.get("id") != expected_id:
                raise ValueError("equipment-error cases are not in fixed order")
            save_dir = args.save_root / expected["save_case"]
            if sha256(save_dir / "SAVE.DA1") != expected["fixture_save_sha256"]:
                raise ValueError(f"{expected_id} staged save differs")
            replay = args.reference.parent / expected["replay"]
            trace_path = args.output / f"{expected_id}-trace.json"
            frame_path = args.output / f"{expected_id}-frames.bin"
            subprocess.run([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(save_dir), "--slot", "1", "--no-save",
                "--run-replay", str(replay),
                "--trace-output", str(trace_path),
                "--frame-output", str(frame_path),
            ], check=True, stdout=subprocess.DEVNULL)

            trace = json.loads(trace_path.read_text(encoding="utf-8"))
            total = expected["rewrite_input_total"]
            if trace.get("input") != {
                    "total": total, "consumed": total, "remaining": 0,
                    "implicit_quit_calls": 0}:
                raise ValueError(f"{expected_id} input accounting differs")
            if trace.get("boundaries") != {
                    "wait": expected["rewrite_wait_boundaries"], "poll": 3,
                    "text": 1, "frontend": 105}:
                raise ValueError(f"{expected_id} input boundaries differ")
            if trace.get("video") != {
                    "frames": expected["rewrite_frames"],
                    "direct_updates": expected["rewrite_direct_updates"],
                    "last_width": 320, "last_height": 200,
                    "fnv1a64": expected["rewrite_video_fnv1a64"]}:
                raise ValueError(f"{expected_id} video summary differs")
            if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                    trace.get("name_fnv1a64")) != (
                        expected["rewrite_state_fnv1a64"], MAPZ_FNV, NAME_FNV):
                raise ValueError(f"{expected_id} mutated the rejected save state")
            hashes = trace.get("frame_fnv1a64", [])
            message_frame = expected["rewrite_message_frame"]
            if len(hashes) != expected["rewrite_frames"] or \
                    hashes[message_frame] != expected["rewrite_message_fnv1a64"] or \
                    hashes[-1] != expected["rewrite_final_fnv1a64"]:
                raise ValueError(f"{expected_id} message/return frames differ")

            frames = load_indexed_frames(frame_path)
            if len(frames) != expected["rewrite_frames"]:
                raise ValueError(f"{expected_id} capture count differs")
            pixels, palette = frames[message_frame]
            rgb = expand_rgb(pixels, palette)
            if hashlib.sha256(pixels).hexdigest() != \
                    expected["rewrite_indexed_sha256"]:
                raise ValueError(f"{expected_id} indexed message page differs")
            if hashlib.sha256(palette).hexdigest() != \
                    expected["rewrite_palette_sha256"]:
                raise ValueError(f"{expected_id} message palette differs")
            if hashlib.sha256(rgb).hexdigest() != expected["rewrite_rgb_sha256"]:
                raise ValueError(f"{expected_id} complete rewrite RGB page differs")
            if hashlib.sha256(rgb_crop(rgb, expected["rgb_box"])).hexdigest() != \
                    expected["matched_rgb_sha256"]:
                raise ValueError(f"{expected_id} original RGB observation differs")

            for name in (
                    "original_png_sha256", "original_rgb_sha256",
                    "capture_video_sha256", "capture_manifest_sha256",
                    "capture_autotype_sha256"):
                value = expected.get(name, "")
                if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                    raise ValueError(f"{expected_id} has malformed {name}")

        print(
            "RPG equipment errors checkpoint: 369a/3728/370e, indexed VGA, "
            "original RGB, acknowledgement, cancellation, and immutable state match")
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG equipment errors checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
