#!/usr/bin/env python3
"""Lock all action-29h travel rows against indexed and original RGB evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from swd2_frame_capture import crop_indexed, expand_rgb, load_indexed_frames


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
                "total": 25, "consumed": 25, "remaining": 0,
                "implicit_quit_calls": 0}:
            raise ValueError("travel replay input accounting differs")
        if trace.get("boundaries") != {
                "wait": 24, "poll": 1, "text": 0, "frontend": 105}:
            raise ValueError("travel replay input boundaries differ")
        video = trace.get("video", {})
        if video != {
                "frames": 130, "direct_updates": 0, "last_width": 320,
                "last_height": 200, "fnv1a64": "d7a266f4c0763b66"}:
            raise ValueError("travel replay video summary differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    "3e76992151d5da88", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("travel replay changed the staged save triple")
        expected_full = [
            "339df31ea02f626a", "8a655a6b70302777", "89e16c1cb94cdb77",
            "257530841f2a5c33", "462b6f340e14d1c1", "7e3141e098cdf957",
            "fd9e196a60833853", "7a918388786b03eb", "e87586dad42702f7",
            "8dd4734f8df2249e", "37e09850c7a49274", "422921797ce45ef6",
        ]
        if trace.get("frame_fnv1a64", [])[118:130] != expected_full:
            raise ValueError("travel destination or scrolling frames differ")

        frames = load_indexed_frames(args.frames)
        entries = reference.get("frames", [])
        if len(frames) != 130 or len(entries) != 12:
            raise ValueError("travel frame count differs")
        box = tuple(reference.get("crop", []))
        first = reference.get("rewrite_first_frame")
        if box != (64, 24, 176, 168) or first != 118:
            raise ValueError("travel reference geometry differs")
        for offset, entry in enumerate(entries):
            if entry.get("destination") != offset:
                raise ValueError("travel destinations are not sequential")
            pixels, palette = frames[first + offset]
            indexed = crop_indexed(pixels, box)
            if hashlib.sha256(indexed).hexdigest() != \
                    entry.get("rewrite_indexed_crop_sha256"):
                raise ValueError(f"travel indexed crop {offset} differs")
            if hashlib.sha256(palette).hexdigest() != \
                    entry.get("rewrite_palette_sha256"):
                raise ValueError(f"travel palette {offset} differs")
            if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != \
                    entry.get("matched_rgb_crop_sha256"):
                raise ValueError(f"travel original RGB crop {offset} differs")
        print(
            "RPG travel checkpoint: 12/12 destination states, both scroll "
            "steps, indexed VGA, palette, and original RGB crops match")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG travel checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
