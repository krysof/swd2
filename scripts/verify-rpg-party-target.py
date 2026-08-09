#!/usr/bin/env python3
"""Lock all four common RPG party-target cards to original full-page RGB."""

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
            raise ValueError("party-target replay input accounting differs")
        if trace.get("boundaries") != {
                "wait": 15, "poll": 1, "text": 0, "frontend": 105}:
            raise ValueError("party-target replay input boundaries differ")
        if trace.get("video") != {
                "frames": 121, "direct_updates": 0, "last_width": 320,
                "last_height": 200, "fnv1a64": "ed23a23a3e1eb3a6"}:
            raise ValueError("party-target replay video summary differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    "ea698a5a2cc29c78", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("party-target replay changed the staged save triple")
        if trace.get("frame_fnv1a64", [])[116:121] != [
                "dc2a6fcf35e70cd7", "198a28f411137c64",
                "60f588a279518820", "1d39effaab194cd0",
                "dc2a6fcf35e70cd7"]:
            raise ValueError("party-target direction frames differ")

        frames = load_indexed_frames(args.frames)
        entries = reference.get("frames", [])
        if len(frames) != 121 or len(entries) != 5 or \
                reference.get("rewrite_first_frame") != 116:
            raise ValueError("party-target reference geometry differs")
        choices = ["left", "right", "up", "down", "left"]
        for offset, entry in enumerate(entries):
            if entry.get("choice") != choices[offset]:
                raise ValueError("party-target choices are not in replay order")
            pixels, palette = frames[116 + offset]
            if hashlib.sha256(pixels).hexdigest() != \
                    entry.get("rewrite_indexed_sha256"):
                raise ValueError(f"party-target indexed page {offset} differs")
            if hashlib.sha256(palette).hexdigest() != \
                    entry.get("rewrite_palette_sha256"):
                raise ValueError(f"party-target palette {offset} differs")
            if hashlib.sha256(expand_rgb(pixels, palette)).hexdigest() != \
                    entry.get("matched_rgb_sha256"):
                raise ValueError(f"party-target original RGB page {offset} differs")
        print(
            "RPG party-target checkpoint: all four members, death/low/status "
            "overlays, indexed VGA, palette, and original full-page RGB match")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG party-target checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
