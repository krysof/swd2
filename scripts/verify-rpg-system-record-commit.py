#!/usr/bin/env python3
"""Verify Record's disk transaction and its immediate original world page."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from swd2_frame_capture import crop_indexed, expand_rgb, load_indexed_frames


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


def fnv1a64(data: bytes) -> str:
    value = FNV_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV_PRIME) & 0xffffffffffffffff
    return f"{value:016x}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("frames", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        trace = json.loads(args.trace.read_text(encoding="utf-8"))
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        if trace.get("input") != {
                "total": 16, "consumed": 16, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 14, "poll": 2, "text": 0, "frontend": 105}:
            raise ValueError("Record commit input boundary differs")
        if trace.get("video") != {
                "frames": 121, "direct_updates": 0, "last_width": 320,
                "last_height": 200, "fnv1a64": "ef734fd06c07643a"}:
            raise ValueError("Record commit video summary differs")
        hashes = trace.get("frame_fnv1a64", [])
        if len(hashes) != 121 or hashes[-3:] != [
                "6a743bf7c3890a74", "30b5d5545fb8bbcb",
                "5ebffbd2aab2de4c"]:
            raise ValueError("Record confirmation or post-commit world differs")

        files = (("SAVE.DA1", "state_fnv1a64"),
                 ("MAPZ.DA1", "mapz_fnv1a64"),
                 ("NAME1.DSK", "name_fnv1a64"))
        for name, trace_key in files:
            if fnv1a64((args.save_root / name).read_bytes()) != trace.get(trace_key):
                raise ValueError(f"committed {name} differs from live state")
        extras = sorted(path.name for path in args.save_root.iterdir()
                        if path.is_file() and path.name not in {x[0] for x in files})
        if extras:
            raise ValueError(f"Record left transaction files behind: {extras}")

        frames = load_indexed_frames(args.frames)
        frame_number = reference.get("rewrite_frame")
        box = tuple(reference.get("crop", []))
        if len(frames) != 121 or frame_number != 120 or box != (0, 0, 320, 200):
            raise ValueError("Record reference frame geometry differs")
        pixels, palette = frames[frame_number]
        indexed = crop_indexed(pixels, box)
        if hashlib.sha256(indexed).hexdigest() != \
                reference.get("rewrite_indexed_crop_sha256"):
            raise ValueError("Record post-commit indexed page differs")
        if hashlib.sha256(palette).hexdigest() != \
                reference.get("rewrite_palette_sha256"):
            raise ValueError("Record post-commit palette differs")
        if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != \
                reference.get("matched_original_rgb_crop_sha256"):
            raise ValueError("Record post-commit original RGB page differs")
        print(
            "RPG Record commit: SAVE/MAPZ/NAME transaction matches live state; "
            "4d55 returns to a 64,000/64,000-pixel original world page")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG Record commit: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
