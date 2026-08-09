#!/usr/bin/env python3
"""Lock the deterministic RPG music/sound System toggle replay."""

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
            "total": 13, "consumed": 13, "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 12, "poll": 1, "text": 0, "frontend": 105,
        }:
            raise ValueError("System toggle input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 118, "direct_updates": 0, "last_width": 320,
            "last_height": 200, "fnv1a64": "d0e9dc3b826cd1ec",
        }:
            raise ValueError("System toggle video summary differs")
        expected_pages = [
            "2f11d877c82ae183", "5b42982322dcd8c3",
            "4d28192901f7a0e3", "05e47e89987f145f",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 118 or hashes[114:118] != expected_pages:
            raise ValueError("System toggle pages differ")
        frames = load_indexed_frames(args.frames)
        if len(frames) != 118:
            raise ValueError("System toggle frame capture count differs")
        indexed_sha256 = [
            "45f2fd56985e54a2150b68634db58ad32f3045aa4010b5d6857cbb1f3e78c5dc",
            "dcd31b26d008c7f75f58c85d73493dd8a5f9b36500b4c93d5ef66f1cb8f54177",
            "5a4ee8d9722b9b40742f7df08806d089a308b61141581edc87d5f4a2dd163af8",
            "91ec9a08420d5bbb581cf42a11a069290935299afd39da5eddab8370054b181c",
        ]
        rgb_sha256 = [
            "fd9cd81af728198968f4997f061e356634c5f2f5cf63fe085aff719fb8d8fa01",
            "52462877e8ee02d1f82e7865f09c39f54bc9e72526d3920b5dbed7abbe1bd746",
            "e8e4702815551985d3d28a8e11b690145f8bfd0ac714d4f1ca893e4094b542af",
            "0f0be6dd732547b0f84a41ee9b729e8c9d272a11a6512b7073bf0b812071f6e0",
        ]
        palette_sha256 = \
            "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960"
        for position, frame_number in enumerate(range(114, 118)):
            pixels, palette = frames[frame_number]
            indexed = crop_indexed(pixels, (80, 10, 186, 122))
            if hashlib.sha256(palette).hexdigest() != palette_sha256:
                raise ValueError(f"System toggle frame {frame_number} palette differs")
            if hashlib.sha256(indexed).hexdigest() != indexed_sha256[position]:
                raise ValueError(f"System toggle frame {frame_number} indices differ")
            if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != \
                    rgb_sha256[position]:
                raise ValueError(f"System toggle frame {frame_number} RGB differs")
        if data.get("audio") != {
            "music_calls": 2, "voice_calls": 0,
            "stop_music_calls": 1, "stop_audio_calls": 2,
            "fnv1a64": "9d6b5970656ebc0f",
        }:
            raise ValueError("System toggle audio call sequence differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "0311d19e2eb0fb4d", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("System toggle final save triple differs")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System toggle replay did not stop explicitly")
        print(
            "RPG System toggles: four indexed/RGB states, save flags and "
            "audio calls locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System toggles: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
