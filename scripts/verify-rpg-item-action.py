#!/usr/bin/env python3
"""Lock all four Item action cards against indexed and original RGB evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("frames", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        trace = json.loads(args.trace.read_text(encoding="utf-8"))
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        if trace.get("schema_version") != 1 or reference.get("schema_version") != 1:
            raise ValueError("unsupported trace/reference schema")
        if trace.get("input") != {
                "total": 16, "consumed": 16, "remaining": 0,
                "implicit_quit_calls": 0}:
            raise ValueError("Item action replay input accounting differs")
        if trace.get("boundaries") != {
                "wait": 15, "poll": 1, "text": 0, "frontend": 105}:
            raise ValueError("Item action replay input boundaries differ")
        if trace.get("video") != {
                "frames": 121, "direct_updates": 0, "last_width": 320,
                "last_height": 200, "fnv1a64": "e35023f6aa642a05"}:
            raise ValueError("Item action replay video summary differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    "de9012ef0f573714", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("Item action replay changed the staged save triple")
        expected_full = [
            "0bd6b4e173c48586", "b3c5e0b1d2653cb1",
            "3e293ee4efc2417b", "7df68e1dcf0ca229",
            "0bd6b4e173c48586",
        ]
        if trace.get("frame_fnv1a64", [])[116:121] != expected_full:
            raise ValueError("Item action direction frames differ")

        frames = load_indexed_frames(args.frames)
        entries = reference.get("frames", [])
        first = reference.get("rewrite_first_frame")
        if len(frames) != 121 or len(entries) != 5 or first != 116:
            raise ValueError("Item action reference geometry differs")
        expected_choices = ["use", "description", "discard", "alchemy", "use"]
        for offset, entry in enumerate(entries):
            if entry.get("choice") != expected_choices[offset]:
                raise ValueError("Item action choices are not in replay order")
            pixels, palette = frames[first + offset]
            if hashlib.sha256(pixels).hexdigest() != \
                    entry.get("rewrite_indexed_sha256"):
                raise ValueError(f"Item action indexed page {offset} differs")
            if hashlib.sha256(palette).hexdigest() != \
                    entry.get("rewrite_palette_sha256"):
                raise ValueError(f"Item action palette {offset} differs")
            if hashlib.sha256(expand_rgb(pixels, palette)).hexdigest() != \
                    entry.get("matched_rgb_sha256"):
                raise ValueError(f"Item action original RGB page {offset} differs")
        print(
            "RPG Item action checkpoint: Use/Description/Discard/Alchemy, "
            "indexed VGA, palette, and original full-page RGB match")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG Item action checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
