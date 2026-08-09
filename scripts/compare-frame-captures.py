#!/usr/bin/env python3
"""Validate and compare exact SWD2 indexed-frame capture streams."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import re
import struct
import sys
from pathlib import Path


MAGIC = b"SWD2FRM2"


@dataclasses.dataclass(frozen=True)
class Frame:
    width: int
    height: int
    direct: bool
    pixels_sha256: str
    palette_sha256: str


@dataclasses.dataclass(frozen=True)
class Capture:
    sha256: str
    size: int
    inputs: tuple[tuple[int, int], ...]
    frames: tuple[Frame, ...]


BOUNDARIES = {"ANY": 0, "WAIT": 1, "POLL": 2, "TEXT": 3, "FRONTEND": 4}
ACTIONS = {
    "TICK": 0,
    "NONE": 0,
    "UP": 1,
    "DOWN": 2,
    "LEFT": 3,
    "RIGHT": 4,
    "PGUP": 5,
    "PAGEUP": 5,
    "PGDN": 6,
    "PAGEDOWN": 6,
    "HOME": 7,
    "END": 8,
    "OK": 9,
    "CONFIRM": 9,
    "ENTER": 9,
    "CANCEL": 10,
    "ESC": 10,
    "ESCAPE": 10,
    "QUIT": 11,
}


def parse_input(path: Path) -> tuple[tuple[int, int], ...]:
    text = path.read_text(encoding="utf-8")
    without_comments = "\n".join(line.split("#", 1)[0] for line in text.splitlines())
    result: list[tuple[int, int]] = []
    for raw in re.split(r"[,\s]+", without_comments):
        if not raw:
            continue
        token = raw.upper()
        count = 1
        if "*" in token:
            token, separator, count_text = token.rpartition("*")
            if separator != "*" or not count_text.isascii() or not count_text.isdigit():
                raise ValueError("invalid pixel-diff replay repeat count")
            count = int(count_text)
            if count < 1 or count > 1_000_000:
                raise ValueError("pixel-diff replay repeat count must be 1..1000000")
        boundary = 0
        if ":" in token:
            if token.count(":") != 1:
                raise ValueError("pixel-diff replay token has multiple boundaries")
            boundary_name, token = token.split(":", 1)
            if boundary_name not in BOUNDARIES:
                raise ValueError(f"unknown pixel-diff replay boundary: {boundary_name}")
            boundary = BOUNDARIES[boundary_name]
        if token not in ACTIONS:
            raise ValueError(f"unknown pixel-diff replay action: {token}")
        action = ACTIONS[token]
        if boundary == BOUNDARIES["FRONTEND"] and action not in (0, 11):
            raise ValueError("FRONTEND replay steps accept only NONE or QUIT")
        if len(result) + count > 1_000_000:
            raise ValueError("pixel-diff replay input exceeds one million actions")
        result.extend([(boundary, action)] * count)
    if not result:
        raise ValueError("pixel-diff replay input is empty")
    return tuple(result)


def load_capture(path: Path) -> Capture:
    digest = hashlib.sha256()
    size = 0
    inputs: list[tuple[int, int]] = []
    frames: list[Frame] = []
    with path.open("rb") as stream:

        def read_exact(count: int, label: str) -> bytes:
            nonlocal size
            value = stream.read(count)
            if len(value) != count:
                raise ValueError(f"{path}: truncated {label}")
            digest.update(value)
            size += len(value)
            return value

        if read_exact(len(MAGIC), "header") != MAGIC:
            raise ValueError(f"{path}: unsupported frame capture format")
        input_count = struct.unpack("<Q", read_exact(8, "input count"))[0]
        if input_count == 0 or input_count > 1_000_000:
            raise ValueError(f"{path}: invalid capture input count {input_count}")
        for index in range(input_count):
            boundary, action = read_exact(2, "input step")
            if boundary > 4 or action > 11:
                raise ValueError(f"{path}: invalid input step {index}")
            if boundary == 4 and action not in (0, 11):
                raise ValueError(f"{path}: invalid FRONTEND input step {index}")
            inputs.append((boundary, action))
        while True:
            tag = read_exact(4, "record tag")
            if tag == b"DONE":
                recorded_count = struct.unpack("<Q", read_exact(8, "trailer"))[0]
                if recorded_count != len(frames):
                    raise ValueError(
                        f"{path}: trailer says {recorded_count} frames, "
                        f"decoded {len(frames)}"
                    )
                if stream.read(1):
                    raise ValueError(f"{path}: bytes follow the DONE trailer")
                break
            if tag != b"FRAM":
                raise ValueError(f"{path}: unknown capture record {tag!r}")
            width, height, direct, reserved = struct.unpack(
                "<IIB3s", read_exact(12, "frame header")
            )
            if width != 320 or height != 200:
                raise ValueError(
                    f"{path}: frame {len(frames)} is {width}x{height}, not 320x200"
                )
            if direct not in (0, 1) or reserved != b"\0\0\0":
                raise ValueError(f"{path}: frame {len(frames)} has invalid flags")
            pixels = read_exact(width * height, "indexed pixels")
            palette = read_exact(768, "VGA palette")
            frames.append(
                Frame(
                    width,
                    height,
                    bool(direct),
                    hashlib.sha256(pixels).hexdigest(),
                    hashlib.sha256(palette).hexdigest(),
                )
            )
    if not frames:
        raise ValueError(f"{path}: capture contains no frames")
    return Capture(digest.hexdigest(), size, tuple(inputs), tuple(frames))


def files_equal(left: Path, right: Path) -> bool:
    with left.open("rb") as first, right.open("rb") as second:
        while True:
            first_chunk = first.read(1024 * 1024)
            second_chunk = second.read(1024 * 1024)
            if first_chunk != second_chunk:
                return False
            if not first_chunk:
                return True


def build_report(input_path: Path, baseline_path: Path, rewrite_path: Path) -> dict[str, object]:
    input_sha256 = hashlib.sha256(input_path.read_bytes()).hexdigest()
    inputs = parse_input(input_path)
    baseline = load_capture(baseline_path)
    rewrite = load_capture(rewrite_path)

    common = min(len(baseline.frames), len(rewrite.frames))
    dimensions = True
    direct_updates = True
    indexed_pixels = True
    palettes = True
    first_difference: dict[str, object] | None = None
    for index in range(common):
        original_frame = baseline.frames[index]
        rewrite_frame = rewrite.frames[index]
        frame_checks = {
            "dimensions": (original_frame.width, original_frame.height)
            == (rewrite_frame.width, rewrite_frame.height),
            "direct_update": original_frame.direct == rewrite_frame.direct,
            "indexed_pixels": original_frame.pixels_sha256
            == rewrite_frame.pixels_sha256,
            "palette": original_frame.palette_sha256
            == rewrite_frame.palette_sha256,
        }
        dimensions = dimensions and frame_checks["dimensions"]
        direct_updates = direct_updates and frame_checks["direct_update"]
        indexed_pixels = indexed_pixels and frame_checks["indexed_pixels"]
        palettes = palettes and frame_checks["palette"]
        if first_difference is None:
            failed = [name for name, passed in frame_checks.items() if not passed]
            if failed:
                first_difference = {"frame": index, "components": failed}

    frame_count = len(baseline.frames) == len(rewrite.frames)
    input_sequence = baseline.inputs == inputs and rewrite.inputs == inputs
    if first_difference is None and not input_sequence:
        first_difference = {"frame": None, "components": ["input_sequence"]}
    if first_difference is None and not frame_count:
        first_difference = {"frame": common, "components": ["frame_count"]}
    exact_bytes = files_equal(baseline_path, rewrite_path)
    if first_difference is None and not exact_bytes:
        first_difference = {"frame": None, "components": ["container_bytes"]}

    checks = {
        "input_sequence": input_sequence,
        "frame_count": frame_count,
        "dimensions": dimensions,
        "direct_updates": direct_updates,
        "indexed_pixels": indexed_pixels,
        "palettes": palettes,
        "exact_capture_bytes": exact_bytes,
    }
    verified = all(checks.values())
    return {
        "schema_version": 1,
        "kind": "pixel_diff_comparison",
        "status": "verified" if verified else "mismatch",
        "input_sha256": input_sha256,
        "baseline": {
            "sha256": baseline.sha256,
            "bytes": baseline.size,
            "frames": len(baseline.frames),
        },
        "rewrite_output": {
            "sha256": rewrite.sha256,
            "bytes": rewrite.size,
            "frames": len(rewrite.frames),
        },
        "checks": checks,
        "first_difference": first_difference,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("rewrite_output", type=Path)
    parser.add_argument("--input", type=Path, required=True)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--output", type=Path)
    group.add_argument("--verify-report", type=Path)
    args = parser.parse_args()
    try:
        report = build_report(args.input, args.baseline, args.rewrite_output)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
        if args.verify_report:
            recorded = json.loads(args.verify_report.read_text(encoding="utf-8"))
            if recorded != report:
                raise ValueError("recorded pixel-diff report differs from live comparison")
        if report["status"] != "verified":
            raise ValueError(f"frame captures diverge: {report['first_difference']}")
        print(
            "frame capture comparison: VERIFIED "
            f"({report['baseline']['frames']} exact 320x200 indexed frames)"
        )
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"frame capture comparison: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
