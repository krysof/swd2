#!/usr/bin/env python3
"""Lock the RPG inventory replay and its original grayscale-page boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from swd2_frame_capture import crop_indexed, expand_rgb


MAGIC = b"SWD2FRM2"


def load_frames(path: Path) -> list[tuple[bytes, bytes]]:
    data = path.read_bytes()
    cursor = 0

    def take(count: int, label: str) -> bytes:
        nonlocal cursor
        result = data[cursor:cursor + count]
        if len(result) != count:
            raise ValueError(f"truncated {label}")
        cursor += count
        return result

    if take(8, "capture magic") != MAGIC:
        raise ValueError("unsupported frame capture")
    input_count = struct.unpack("<Q", take(8, "input count"))[0]
    take(input_count * 2, "input sequence")
    frames: list[tuple[bytes, bytes]] = []
    while True:
        tag = take(4, "record tag")
        if tag == b"DONE":
            recorded = struct.unpack("<Q", take(8, "frame count"))[0]
            if recorded != len(frames) or cursor != len(data):
                raise ValueError("invalid frame capture trailer")
            return frames
        if tag != b"FRAM":
            raise ValueError(f"unknown frame record {tag!r}")
        width, height, direct, reserved = struct.unpack(
            "<IIB3s", take(12, "frame header"))
        if (width, height, direct, reserved) != (320, 200, 0, b"\0\0\0"):
            raise ValueError("unexpected inventory frame header")
        pixels = take(320 * 200, "indexed pixels")
        palette = take(768, "VGA palette")
        frames.append((pixels, palette))


def translated(color: int, palette: bytes) -> int:
    # RPG:0dbf:0314 preserves its 16-entry grayscale target ramp and
    # translates every other source index through the saved DAC palette.
    if 0x10 <= color < 0x20:
        return color
    red, green, blue = palette[color * 3:color * 3 + 3]
    return 0x1f - ((red + green * 2 + blue) >> 4)


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
            "total": 60,
            "consumed": 60,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("inventory replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 59,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("inventory replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 165 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "40add277e364ebe8":
            raise ValueError("inventory replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        inventory_pages = [
            "6fef253f12fc36a0", "6cbf439a2407111d",
            "ff7d40657fcc8959", "9d750d9ad943ab4d",
            "f6ed6b10c8dba38d", "d3c218c4f3783e55",
            "4ab42a91492fcc89", "0a70790315cca256",
            "5c5f90d2a1a7338a", "2bff2d80b19f0176",
            "7805e862f5dc99ee", "0039c549698653e2",
            "c338ba98d673aeee", "4233a7a9232b396a",
            "00f47900d578a892", "93880fb2a24512ae",
            "73b690263a8e29c2", "49102b5625f95346",
            "48c3779302c46b22", "19f7d9a1098197b2",
            "d430b1ec54bb555a", "02ed3370b163fcf6",
            "7b5bb58d806adcbe", "05ca539615449c72",
            "cf9a9a283ac9572e", "873144dc288f489e",
            "75a6e8e7e6bb8f96", "8c159c86c98c1e3e",
            "9a364b5e8f5bec9a", "74865a73486d4692",
            "0c00d29609ac21ce", "f8e9e9e513596402",
            "7db604b6bc953e62", "fd49cd67f70ab292",
            "495decd73fab899a", "0eff905a8ba8f4ee",
            "434666ab7b8b36ca", "90732ae4f778c3de",
            "dc8d3301e64f75fe", "2de46925295155ba",
            "18121965359ea6ae", "3285207c99cc40e6",
            "a813bf0301d97372", "4419d9a570260f6e",
            "1e36441e22112e4a", "013ce6fb3a66241a",
            "92d481a0f315b792", "93f38bfd65ea1a56",
            "d5937ec6cc1a134e", "f99bd38e83683e0e",
        ]
        if len(hashes) != 165 or hashes[113:115] != [
                "1b0a18f362ed2cb2",  # System/Book-selected field diamond
                "ddb26857dd11b4f3",  # Item-selected field diamond
        ] or hashes[115:165] != inventory_pages:
            raise ValueError("inventory selection pages differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("inventory replay changed the loaded save triple")

        frames = load_frames(args.frames)
        if len(frames) != 165:
            raise ValueError("inventory frame capture count differs")
        source_pixels, source_palette = frames[114]
        inventory_pixels, inventory_palette = frames[115]
        # RPG:3a00/3a05 loads CD000.RSK and copies exactly palette entries
        # 10h..ffh into DATA:5a8c after 3a48 has converted the world through
        # the map palette.  The first sixteen entries stay map-owned and the
        # resulting hardware palette persists for every 39ed item page.
        if source_palette[:0x10 * 3] != inventory_palette[:0x10 * 3] or \
                hashlib.sha256(inventory_palette[0x10 * 3:]).hexdigest() != \
                "0a00f050f396a9fdf48cd10cb7ca5a7b5ec1d7d174470fcc244242667aeb9c29":
            raise ValueError("inventory CD000 palette splice differs")
        if any(palette != inventory_palette for _, palette in frames[115:]):
            raise ValueError("inventory CD000 palette did not persist")
        original_crops = [
            ((16, 16, 64, 96),
             "9bcb1bab325abc7a72a49dcdac8e0b38717d257b307158c44c369dc4d56d785b",
             "e3e4e366a10aeec19d842924887c3579445172042944ea01917c93d68d4cefde"),
            ((236, 12, 56, 24),
             "c34b669c588de4ceba511ff2afb18b8b895c9bbcee93029d8de92b2b91ec79e6",
             "fa3c9edc3e3010b1732a9d781bc84b6e9a0bb5de5bf83cd58e5e510837742600"),
            ((120, 68, 168, 112),
             "0bb9c09b23a949e60673af9854433f9e1f6c0353f44f45847019fd4f1c4c8c67",
             "16f2095d0a2c61fe2e786b327c94ad787329850318046e9ae77283309acf760f"),
        ]
        for box, expected_indices, expected_rgb in original_crops:
            indexed = crop_indexed(inventory_pixels, box)
            if hashlib.sha256(indexed).hexdigest() != expected_indices:
                raise ValueError(f"inventory indexed crop differs: {box}")
            if hashlib.sha256(expand_rgb(indexed, inventory_palette)).hexdigest() != \
                    expected_rgb:
                raise ValueError(f"inventory original RGB crop differs: {box}")
        checked: set[int] = set()
        # These unobscured top and left strips remain the same world page
        # between the Item-selected diamond and the first inventory page.
        for y in range(8):
            checked.update(range(y * 320, (y + 1) * 320))
        for y in range(8, 200):
            checked.update(range(y * 320, y * 320 + 8))
        mismatched = sum(
            translated(source_pixels[index], source_palette) !=
            inventory_pixels[index]
            for index in checked)
        if mismatched:
            raise ValueError(
                f"inventory grayscale world boundary differs at "
                f"{mismatched}/{len(checked)} unobscured pixels")

        print(
            "RPG inventory checkpoint: 165 frames, all 50 rows locked, "
            f"{len(checked)} grayscale world pixels exact, and three "
            "original-matched UI crops locked as indexed VGA")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG inventory checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
