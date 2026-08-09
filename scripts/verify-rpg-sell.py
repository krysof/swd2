#!/usr/bin/env python3
"""Lock twelve selling rows against indexed and original full-page RGB evidence."""

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
                "total": 23, "consumed": 23, "remaining": 0,
                "implicit_quit_calls": 0}:
            raise ValueError("selling replay input accounting differs")
        if trace.get("boundaries") != {
                "wait": 21, "poll": 1, "text": 1, "frontend": 105}:
            raise ValueError("selling replay input boundaries differ")
        if trace.get("video") != {
                "frames": 133, "direct_updates": 2, "last_width": 320,
                "last_height": 200, "fnv1a64": "fdcb8c1eb4bbd10f"}:
            raise ValueError("selling replay video summary differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    "5b6d560a1d793d08", "aa5004efb4713eee",
                    "e3d2853e2676513b"):
            raise ValueError("selling replay changed the staged save triple")
        expected_full = [
            "2cd4ef15a1e130e3", "5a6859650f76ac74",
            "7aa86aec7d6f590a", "03c0ed8f76dca47a",
            "d38484ddae7db752", "2baf284a5bb21492",
            "48f0cad325e8f0d0", "79d2d7162ec7f0f1",
            "1ad95bfb26fe7a75", "8f14f6d5e066a957",
            "c874b3331637fb67", "860438ead1ddde6f",
        ]
        if trace.get("frame_fnv1a64", [])[121:133] != expected_full:
            raise ValueError("selling rows or scrolling frames differ")

        frames = load_indexed_frames(args.frames)
        entries = reference.get("frames", [])
        first = reference.get("rewrite_first_frame")
        if len(frames) != 133 or len(entries) != 12 or first != 121:
            raise ValueError("selling reference geometry differs")
        for offset, entry in enumerate(entries):
            if entry.get("selection") != offset:
                raise ValueError("selling selections are not sequential")
            pixels, palette = frames[first + offset]
            if hashlib.sha256(pixels).hexdigest() != \
                    entry.get("rewrite_indexed_sha256"):
                raise ValueError(f"selling indexed page {offset} differs")
            if hashlib.sha256(palette).hexdigest() != \
                    entry.get("rewrite_palette_sha256"):
                raise ValueError(f"selling palette {offset} differs")
            if hashlib.sha256(expand_rgb(pixels, palette)).hexdigest() != \
                    entry.get("matched_rgb_sha256"):
                raise ValueError(f"selling original RGB page {offset} differs")
        print(
            "RPG selling checkpoint: 12/12 items, all four scroll steps, "
            "type cards, indexed VGA, palette, and original full-page RGB match")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG selling checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
