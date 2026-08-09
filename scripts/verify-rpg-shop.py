#!/usr/bin/env python3
"""Lock all eight opcode-19 shop rows against indexed and original RGB evidence."""

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
                "total": 17, "consumed": 17, "remaining": 0,
                "implicit_quit_calls": 0}:
            raise ValueError("shop replay input accounting differs")
        if trace.get("boundaries") != {
                "wait": 15, "poll": 1, "text": 1, "frontend": 109}:
            raise ValueError("shop replay input boundaries differ")
        if trace.get("video") != {
                "frames": 126, "direct_updates": 2, "last_width": 320,
                "last_height": 200, "fnv1a64": "1630704f5893df58"}:
            raise ValueError("shop replay video summary differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    "6b7e815377028eeb", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("shop replay changed the staged save triple")
        expected_full = [
            "439806bb5f0a0073", "55db5cc775d07562",
            "21398e530251f240", "3b596f04242fc690",
            "947328efee37cc55", "8b906258905182c7",
            "479cf6ad88193751", "619e4b0e939c5c29",
        ]
        if trace.get("frame_fnv1a64", [])[118:126] != expected_full:
            raise ValueError("shop goods or scrolling frames differ")

        frames = load_indexed_frames(args.frames)
        entries = reference.get("frames", [])
        first = reference.get("rewrite_first_frame")
        if len(frames) != 126 or len(entries) != 8 or first != 118:
            raise ValueError("shop reference geometry differs")
        for offset, entry in enumerate(entries):
            if entry.get("selection") != offset:
                raise ValueError("shop selections are not sequential")
            pixels, palette = frames[first + offset]
            if hashlib.sha256(pixels).hexdigest() != \
                    entry.get("rewrite_indexed_sha256"):
                raise ValueError(f"shop indexed page {offset} differs")
            if hashlib.sha256(palette).hexdigest() != \
                    entry.get("rewrite_palette_sha256"):
                raise ValueError(f"shop palette {offset} differs")
            if hashlib.sha256(expand_rgb(pixels, palette)).hexdigest() != \
                    entry.get("matched_rgb_sha256"):
                raise ValueError(f"shop original RGB page {offset} differs")
        print(
            "RPG shop checkpoint: 8/8 goods, all three scroll steps, prices, "
            "indexed VGA, palette, and original full-page RGB match")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG shop checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
