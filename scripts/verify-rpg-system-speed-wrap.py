#!/usr/bin/env python3
"""Lock all five RPG System speed values and the original right wrap."""

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
    args = parser.parse_args()
    try:
        data = json.loads(args.trace.read_text(encoding="utf-8"))
        if data.get("schema_version") != 1:
            raise ValueError("unsupported trace schema")
        if data.get("input") != {
            "total": 20, "consumed": 20, "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 19, "poll": 1, "text": 0, "frontend": 105,
        }:
            raise ValueError("System speed input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 125, "direct_updates": 0, "last_width": 320,
            "last_height": 200, "fnv1a64": "befa72ffe91d7114",
        }:
            raise ValueError("System speed video summary differs")
        expected_pages = [
            "1c34596f4dbbac5b", "4e869b29e122695b",
            "f16de67152c3a55b", "445c73f8d1b4945b",
            "310502c4797b465b", "1c34596f4dbbac5b",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 125 or hashes[119:125] != expected_pages:
            raise ValueError("System speed pages or right wrap differ")
        frames = load_indexed_frames(args.frames)
        if len(frames) != 125:
            raise ValueError("System speed frame capture count differs")
        indexed_sha256 = [
            "44a5ad7c708deadd2741aa1d2de7bfbd33301daff3c69091c9d1068d04620940",
            "36e5c8b7b977519c72de5d411e4b9679d9947c9a89d4ba235dfddaefcee5ddaa",
            "aa86b2b1e2d88245784aeff65f1d4847e84ce101bfd67e1b5ae88a963f557a94",
            "6295ae3178475663be9afd74ca44c432c7dddec489f74e938eec51d0da3214aa",
            "4581b42187cf4dc6c19261027547ddc375c7d6a8853da4af0397af42ab07dee0",
            "44a5ad7c708deadd2741aa1d2de7bfbd33301daff3c69091c9d1068d04620940",
        ]
        rgb_sha256 = [
            "be429ae0ff37ef93899bce1bdb7c4d627ab4fca7241bf9a1f53dc787d2f6e7a3",
            "23ff95cc03930707fb1cfbbcaf3ecb3fb08e7eae5797dc0ac8265292d7c0b673",
            "fbe9191ed1a6639dc9f0d862dff3ff9e4c61f54b88820e0eb82f831aeccee38b",
            "970b4fc99ddf0b5768e1ac535915b0063c3dcc38b543e6fd9dd59d8734ef2eec",
            "b4116bdeb7a09e43d1dd5994e3ffb4fc5b3c6f99d8e3382d0fe4bd3759b24897",
            "be429ae0ff37ef93899bce1bdb7c4d627ab4fca7241bf9a1f53dc787d2f6e7a3",
        ]
        palette_sha256 = \
            "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960"
        for position, frame_number in enumerate(range(119, 125)):
            pixels, palette = frames[frame_number]
            indexed = crop_indexed(pixels, (16, 116, 284, 80))
            if hashlib.sha256(palette).hexdigest() != palette_sha256:
                raise ValueError(f"System speed frame {frame_number} palette differs")
            if hashlib.sha256(indexed).hexdigest() != indexed_sha256[position]:
                raise ValueError(f"System speed frame {frame_number} indices differ")
            if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != \
                    rgb_sha256[position]:
                raise ValueError(f"System speed frame {frame_number} RGB differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("unconfirmed speed selector changed the save triple")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System speed replay did not stop explicitly")
        print(
            "RPG System speed: indexed/RGB values 1..5 and Right wrap locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System speed: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
