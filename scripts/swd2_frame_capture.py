#!/usr/bin/env python3
"""Strict reader and VGA helpers for deterministic SWD2FRM2 captures."""

from __future__ import annotations

import struct
from pathlib import Path


MAGIC = b"SWD2FRM2"


def load_indexed_frames(path: Path) -> list[tuple[bytes, bytes]]:
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
        if (width, height) != (320, 200) or direct not in (0, 1) or \
                reserved != b"\0\0\0":
            raise ValueError("unexpected indexed VGA frame header")
        frames.append((
            take(320 * 200, "indexed pixels"),
            take(768, "VGA palette"),
        ))


def crop_indexed(
        pixels: bytes, box: tuple[int, int, int, int]) -> bytes:
    x, y, width, height = box
    if x < 0 or y < 0 or width <= 0 or height <= 0 or \
            x + width > 320 or y + height > 200:
        raise ValueError("indexed crop lies outside 320x200 page")
    return b"".join(
        pixels[row * 320 + x:row * 320 + x + width]
        for row in range(y, y + height))


def expand_rgb(indexed: bytes, palette: bytes) -> bytes:
    if len(palette) != 768 or any(component > 63 for component in palette):
        raise ValueError("palette is not 256-entry VGA 6-bit DAC data")
    output = bytearray(len(indexed) * 3)
    for index, color in enumerate(indexed):
        for component in range(3):
            value = palette[color * 3 + component]
            output[index * 3 + component] = (value << 2) | (value >> 4)
    return bytes(output)
