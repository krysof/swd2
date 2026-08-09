#!/usr/bin/env python3
"""Validate the deterministic replay trace schema and completion invariants."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


HEX64 = re.compile(r"^[0-9a-f]{16}$")


def fail(message: str) -> None:
    raise ValueError(message)


def nonnegative_int(value: object, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        fail(f"{label} must be a nonnegative integer")
    return value


def digest(value: object, label: str, nullable: bool = False) -> None:
    if nullable and value is None:
        return
    if not isinstance(value, str) or HEX64.fullmatch(value) is None:
        fail(f"{label} must be a 16-digit lowercase FNV-1a digest")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--require-module", action="append", default=[])
    parser.add_argument("--require-full-game", action="store_true")
    parser.add_argument("--min-frames", type=int, default=1)
    args = parser.parse_args()
    try:
        data = json.loads(args.trace.read_text(encoding="utf-8"))
        if data.get("schema_version") != 1:
            fail("trace schema_version is not 1")

        inputs = data.get("input")
        if not isinstance(inputs, dict):
            fail("trace input summary is missing")
        total = nonnegative_int(inputs.get("total"), "input.total")
        consumed = nonnegative_int(inputs.get("consumed"), "input.consumed")
        remaining = nonnegative_int(inputs.get("remaining"), "input.remaining")
        implicit = nonnegative_int(
            inputs.get("implicit_quit_calls"), "input.implicit_quit_calls"
        )
        if total == 0 or consumed != total or remaining != 0 or implicit != 0:
            fail("trace did not explicitly consume its complete nonempty input")

        checkpoints = data.get("input_checkpoints")
        if not isinstance(checkpoints, list) or len(checkpoints) != consumed:
            fail("trace does not contain one state checkpoint per consumed input")
        for index, checkpoint in enumerate(checkpoints):
            if not isinstance(checkpoint, dict) or checkpoint.get("index") != index:
                fail("trace input checkpoint indexes are not contiguous")
            if checkpoint.get("boundary") not in {
                "WAIT",
                "POLL",
                "TEXT",
                "FRONTEND",
            }:
                fail(f"input checkpoint {index} has an invalid boundary")
            if not isinstance(checkpoint.get("action"), str):
                fail(f"input checkpoint {index} has no action")
            digest(checkpoint.get("state_fnv1a64"), f"input_checkpoints[{index}].state")
            digest(
                checkpoint.get("mapz_fnv1a64"),
                f"input_checkpoints[{index}].mapz",
                nullable=True,
            )

        boundaries = data.get("boundaries")
        if not isinstance(boundaries, dict):
            fail("trace boundary counters are missing")
        for name in ("wait", "poll", "text", "frontend"):
            nonnegative_int(boundaries.get(name), f"boundaries.{name}")

        video = data.get("video")
        if not isinstance(video, dict):
            fail("trace video summary is missing")
        frames = nonnegative_int(video.get("frames"), "video.frames")
        direct = nonnegative_int(video.get("direct_updates"), "video.direct_updates")
        if frames < args.min_frames or direct > frames:
            fail("trace frame count is below the required complete sequence")
        if video.get("last_width") != 320 or video.get("last_height") != 200:
            fail("trace did not end on the original 320x200 surface")
        digest(video.get("fnv1a64"), "video.fnv1a64")
        frame_hashes = data.get("frame_fnv1a64")
        if not isinstance(frame_hashes, list) or len(frame_hashes) != frames:
            fail("trace does not contain exactly one digest per submitted frame")
        for index, frame_hash in enumerate(frame_hashes):
            digest(frame_hash, f"frame_fnv1a64[{index}]")

        audio = data.get("audio")
        if not isinstance(audio, dict):
            fail("trace audio summary is missing")
        for name in (
            "music_calls",
            "voice_calls",
            "stop_music_calls",
            "stop_audio_calls",
        ):
            nonnegative_int(audio.get(name), f"audio.{name}")
        digest(audio.get("fnv1a64"), "audio.fnv1a64")
        nonnegative_int(data.get("delay_milliseconds"), "delay_milliseconds")
        digest(data.get("state_fnv1a64"), "state_fnv1a64")
        digest(data.get("mapz_fnv1a64"), "mapz_fnv1a64", nullable=True)

        transitions = data.get("transitions")
        if not isinstance(transitions, list) or not transitions:
            fail("trace has no module transitions")
        modules: set[str] = set()
        for transition in transitions:
            if not isinstance(transition, dict):
                fail("trace transition is not an object")
            module = transition.get("module")
            if not isinstance(module, str) or not module:
                fail("trace transition has no module name")
            if transition.get("launched") is not True:
                fail(f"trace module did not launch: {module}")
            for marker in ("input", "output"):
                if not isinstance(transition.get(marker), str):
                    fail(f"trace transition {marker} marker is missing")
            modules.add(module)

        required = set(args.require_module)
        if args.require_full_game:
            required.update({"MEO.EXE", "RPG.EXE", "FIG.EXE", "DEMO.EXE"})
        missing = sorted(required - modules)
        if missing:
            fail(f"trace is missing required modules: {missing}")
        if not isinstance(data.get("stop_reason"), str) or not isinstance(
            data.get("final_marker"), str
        ):
            fail("trace final stop state is missing")

        print(
            f"replay trace: OK ({frames} frames, {consumed} inputs, "
            f"{len(transitions)} transitions)"
        )
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"replay trace: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
