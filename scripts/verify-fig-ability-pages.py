#!/usr/bin/env python3
"""Replay and lock FIG's five original ability resource-card pages."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


MAPZ_FNV = "827f0f1b725a0958"
NAME_FNV = "e3d2853e2676513b"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def crop_rgb(rgb: bytes, box: list[int]) -> bytes:
    if len(box) != 4:
        raise ValueError("FIG RGB box must contain four coordinates")
    left, top, right, bottom = box
    if not (0 <= left < right <= 320 and 0 <= top < bottom <= 200):
        raise ValueError("FIG RGB box is outside 320x200")
    return b"".join(
        rgb[y * 960 + left * 3:y * 960 + right * 3]
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
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != "original_fig_ability_resource_pages" or \
                [case.get("ability_id") for case in cases] != [1, 4, 33, 3, 41]:
            raise ValueError("unsupported FIG ability-page reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference.get("reference_program_sha256"):
            raise ValueError("FIG.EXE differs from the captured reference")
        autotype = args.reference.with_name(reference["capture_autotype"])
        if sha256(autotype.read_bytes()) != \
                reference.get("capture_autotype_sha256"):
            raise ValueError("original FIG capture input differs")
        for name in (
                "reference_program_sha256", "capture_harness_sha256",
                "capture_autotype_sha256"):
            value = reference.get(name, "")
            if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                raise ValueError(f"malformed common evidence digest {name}")

        replay = args.reference.with_name("replay-fig-ability-page.txt")
        args.output.mkdir(parents=True, exist_ok=True)
        box = reference["rgb_box"]
        for expected in cases:
            ability_id = expected["ability_id"]
            save_dir = args.save_root / expected["save_case"]
            if sha256((save_dir / "SAVE.DA1").read_bytes()) != \
                    expected["fixture_save_sha256"]:
                raise ValueError(f"ability {ability_id} staged save differs")
            trace_path = args.output / f"ability-{ability_id}-trace.json"
            frame_path = args.output / f"ability-{ability_id}-frames.bin"
            subprocess.run([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(save_dir), "--slot", "1", "--no-save",
                "--start-marker", "IF", "--run-replay", str(replay),
                "--trace-output", str(trace_path),
                "--frame-output", str(frame_path),
            ], check=True, stdout=subprocess.DEVNULL)

            trace = json.loads(trace_path.read_text(encoding="utf-8"))
            if trace.get("input") != {
                    "total": 3, "consumed": 3, "remaining": 0,
                    "implicit_quit_calls": 0} or trace.get("boundaries") != {
                        "wait": 3, "poll": 0, "text": 0, "frontend": 0}:
                raise ValueError(f"ability {ability_id} input boundaries differ")
            if trace.get("video") != {
                    "frames": 4, "direct_updates": 0,
                    "last_width": 320, "last_height": 200,
                    "fnv1a64": expected["rewrite_video_fnv1a64"]}:
                raise ValueError(f"ability {ability_id} video summary differs")
            if trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
                raise ValueError(f"ability {ability_id} final page differs")
            if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                    trace.get("name_fnv1a64")) != (
                        expected["rewrite_state_fnv1a64"], MAPZ_FNV, NAME_FNV):
                raise ValueError(f"ability {ability_id} final shared state differs")
            if trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
                raise ValueError(f"ability {ability_id} did not use direct IF entry")

            frames = load_indexed_frames(frame_path)
            if len(frames) != 4:
                raise ValueError(f"ability {ability_id} frame count differs")
            pixels, palette = frames[-1]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != expected["rewrite_indexed_sha256"] or \
                    sha256(palette) != expected["rewrite_palette_sha256"] or \
                    sha256(rgb) != expected["rewrite_rgb_sha256"]:
                raise ValueError(f"ability {ability_id} indexed VGA page differs")
            crop_digest = sha256(crop_rgb(rgb, box))
            if expected.get("crop") != box or \
                    expected.get("original_crop_rgb_sha256") != crop_digest or \
                    expected.get("rewrite_crop_rgb_sha256") != crop_digest or \
                    expected.get("matched_rgb_sha256") != crop_digest:
                raise ValueError(
                    f"ability {ability_id} no longer matches original RGB")
            for name in (
                    "original_png_sha256", "original_rgb_sha256",
                    "capture_video_sha256", "capture_manifest_sha256"):
                value = expected.get(name, "")
                if len(value) != 64 or \
                        any(c not in "0123456789abcdef" for c in value):
                    raise ValueError(
                        f"ability {ability_id} has malformed {name}")

        print(
            "FIG ability-page checkpoint: IF direct entry, five resource "
            "classes, indexed VGA pages, and original RGB rows 0..197 match")
        return 0
    except (OSError, ValueError, KeyError, IndexError, json.JSONDecodeError,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG ability-page checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
