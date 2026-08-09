#!/usr/bin/env python3
"""Compare hashed DOSBox RGB PNGs with selected SWD2FRM2 rewrite frames."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - environment diagnostic
    raise SystemExit("Pillow is required to decode DOSBox PNG references") from error


MAGIC = b"SWD2FRM2"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_rewrite_rgb(path: Path) -> list[bytes]:
    data = path.read_bytes()
    cursor = 0

    def take(count: int, label: str) -> bytes:
        nonlocal cursor
        value = data[cursor : cursor + count]
        if len(value) != count:
            raise ValueError(f"truncated {label}")
        cursor += count
        return value

    if take(8, "capture magic") != MAGIC:
        raise ValueError("unsupported rewrite frame capture")
    input_count = struct.unpack("<Q", take(8, "input count"))[0]
    if input_count == 0 or input_count > 1_000_000:
        raise ValueError("invalid rewrite input count")
    take(input_count * 2, "input sequence")
    frames: list[bytes] = []
    while True:
        tag = take(4, "record tag")
        if tag == b"DONE":
            recorded = struct.unpack("<Q", take(8, "frame count"))[0]
            if recorded != len(frames) or cursor != len(data):
                raise ValueError("invalid rewrite capture trailer")
            return frames
        if tag != b"FRAM":
            raise ValueError(f"unknown rewrite record {tag!r}")
        width, height, direct, reserved = struct.unpack(
            "<IIB3s", take(12, "frame header")
        )
        if (width, height) != (320, 200) or direct not in (0, 1) or reserved != b"\0\0\0":
            raise ValueError("invalid rewrite frame header")
        pixels = take(width * height, "indexed pixels")
        palette = take(768, "VGA palette")
        if any(component > 63 for component in palette):
            raise ValueError("rewrite palette is not VGA 6-bit DAC data")
        rgb = bytearray(width * height * 3)
        for pixel_index, palette_index in enumerate(pixels):
            for component in range(3):
                value = palette[palette_index * 3 + component]
                rgb[pixel_index * 3 + component] = (value << 2) | (value >> 4)
        frames.append(bytes(rgb))


def load_dosbox_rgb(path: Path) -> bytes:
    with Image.open(path) as source:
        image = source.convert("RGB")
    if image.size == (640, 400):
        pixels = image.load()
        for y in range(0, 400, 2):
            for x in range(0, 640, 2):
                value = pixels[x, y]
                if (pixels[x + 1, y] != value or pixels[x, y + 1] != value or
                        pixels[x + 1, y + 1] != value):
                    raise ValueError(f"{path}: 640x400 reference is not exact 2x VGA pixels")
        image = image.resize((320, 200), Image.Resampling.NEAREST)
    if image.size != (320, 200):
        raise ValueError(f"{path}: expected 320x200 or exact-doubled 640x400 PNG")
    return image.tobytes()


def crop_rgb(rgb: bytes, crop: object) -> tuple[bytes, int]:
    if (not isinstance(crop, list) or len(crop) != 4 or
            any(type(value) is not int for value in crop)):
        raise ValueError("crop must be [x, y, width, height] integers")
    x, y, width, height = crop
    if x < 0 or y < 0 or width <= 0 or height <= 0 or \
            x + width > 320 or y + height > 200:
        raise ValueError("crop lies outside the 320x200 VGA page")
    rows = []
    stride = 320 * 3
    for row in range(y, y + height):
        start = row * stride + x * 3
        rows.append(rgb[start:start + width * 3])
    return b"".join(rows), width * height


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("baseline_directory", type=Path)
    parser.add_argument("rewrite_capture", type=Path)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or manifest.get("kind") != "dosbox_rgb_spot_check":
        raise SystemExit("unsupported DOSBox RGB spot-check manifest")
    if args.game is not None:
        reference_name = manifest.get("reference_program", "RPG.EXE")
        if (not isinstance(reference_name, str) or
                not reference_name.upper().endswith((".EXE", ".COM")) or
                Path(reference_name).name != reference_name):
            raise SystemExit("invalid reference program in manifest")
        expected_program = manifest.get(
            "reference_program_sha256", manifest.get("program_sha256"))
        actual_program = sha256(args.game / reference_name)
        if actual_program != expected_program:
            raise SystemExit(
                f"{reference_name} does not match the reference capture")

    frames = load_rewrite_rgb(args.rewrite_capture)
    results: list[dict[str, object]] = []
    for entry in manifest.get("frames", []):
        original_path = args.baseline_directory / entry["original"]
        actual_sha = sha256(original_path)
        if actual_sha != entry["sha256"]:
            raise SystemExit(f"DOSBox reference hash differs: {original_path}")
        rewrite_index = int(entry["rewrite_frame"])
        if rewrite_index < 0 or rewrite_index >= len(frames):
            raise SystemExit(f"rewrite frame index is absent: {rewrite_index}")
        original = load_dosbox_rgb(original_path)
        rewrite = frames[rewrite_index]
        pixel_count = 320 * 200
        crop = entry.get("crop")
        if crop is not None:
            try:
                original, pixel_count = crop_rgb(original, crop)
                rewrite, rewrite_pixel_count = crop_rgb(rewrite, crop)
            except ValueError as error:
                raise SystemExit(f"{entry['label']}: {error}") from error
            if rewrite_pixel_count != pixel_count:  # defensive symmetry
                raise SystemExit(f"{entry['label']}: inconsistent crop size")
        mismatched = sum(
            original[offset : offset + 3] != rewrite[offset : offset + 3]
            for offset in range(0, len(original), 3)
        )
        result = {
            "label": entry["label"],
            "original": entry["original"],
            "rewrite_frame": rewrite_index,
            "mismatched_rgb_pixels": mismatched,
            "compared_rgb_pixels": pixel_count,
        }
        if crop is not None:
            result["crop"] = crop
        results.append(result)
        if mismatched:
            raise SystemExit(
                f"{entry['label']}: {mismatched}/{pixel_count} RGB pixels differ"
            )

    report = {
        "schema_version": 1,
        "kind": "dosbox_rgb_spot_check_report",
        "status": "verified",
        "limitation": manifest.get("limitation"),
        "rewrite_capture_sha256": sha256(args.rewrite_capture),
        "frames": results,
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    compared = ", ".join(
        f"0/{result['compared_rgb_pixels']}" for result in results)
    print(f"DOSBox RGB spot check: {len(results)} frames, {compared} pixels differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
