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
        audio_counts: dict[str, int] = {}
        for name in (
            "music_calls",
            "voice_calls",
            "stop_music_calls",
            "stop_audio_calls",
        ):
            audio_counts[name] = nonnegative_int(audio.get(name), f"audio.{name}")
        digest(audio.get("fnv1a64"), "audio.fnv1a64")
        total_delay = nonnegative_int(
            data.get("delay_milliseconds"), "delay_milliseconds"
        )

        timeline = data.get("timeline")
        if not isinstance(timeline, list) or not timeline:
            fail("trace unified presentation timeline is missing")
        event_counts = {
            "frame": 0,
            "input": 0,
            "music": 0,
            "voice": 0,
            "stop_music": 0,
            "stop_audio": 0,
        }
        direct_events = 0
        elapsed = 0
        for sequence, event in enumerate(timeline):
            if not isinstance(event, dict) or event.get("sequence") != sequence:
                fail("trace timeline sequence numbers are not contiguous")
            at = nonnegative_int(
                event.get("at_milliseconds"),
                f"timeline[{sequence}].at_milliseconds",
            )
            if at != elapsed:
                fail(f"timeline event {sequence} is not at the accumulated delay")
            kind = event.get("kind")
            if kind == "delay":
                milliseconds = nonnegative_int(
                    event.get("milliseconds"),
                    f"timeline[{sequence}].milliseconds",
                )
                if milliseconds == 0:
                    fail(f"timeline delay {sequence} is empty")
                elapsed += milliseconds
                continue
            if kind not in event_counts:
                fail(f"timeline event {sequence} has an invalid kind")
            index_key = "frame" if kind == "frame" else (
                "input" if kind == "input" else "call"
            )
            index = nonnegative_int(
                event.get(index_key), f"timeline[{sequence}].{index_key}"
            )
            if index != event_counts[kind]:
                fail(f"timeline {kind} indexes are not contiguous")
            event_counts[kind] += 1
            if kind == "frame":
                if not isinstance(event.get("direct"), bool):
                    fail(f"timeline frame {index} has no direct-update flag")
                direct_events += int(event["direct"])
                digest(event.get("fnv1a64"), f"timeline frame {index}")
                if index >= len(frame_hashes) or event["fnv1a64"] != frame_hashes[index]:
                    fail(f"timeline frame {index} digest differs from frame list")
            elif kind == "input":
                if index >= len(checkpoints):
                    fail(f"timeline input {index} is outside the checkpoint list")
                checkpoint = checkpoints[index]
                if event.get("boundary") != checkpoint.get("boundary") or event.get(
                    "action"
                ) != checkpoint.get("action"):
                    fail(f"timeline input {index} differs from its checkpoint")
                digest(event.get("state_fnv1a64"), f"timeline input {index} state")
                if event["state_fnv1a64"] != checkpoint.get("state_fnv1a64"):
                    fail(f"timeline input {index} state differs from its checkpoint")
            elif kind in {"music", "voice"}:
                digest(
                    event.get("payload_fnv1a64"),
                    f"timeline {kind} call {index} payload",
                )
                if kind == "music" and not isinstance(event.get("loop"), bool):
                    fail(f"timeline music call {index} has no loop flag")

        if elapsed != total_delay:
            fail("timeline delays do not add up to delay_milliseconds")
        if event_counts["frame"] != frames or direct_events != direct:
            fail("timeline video events differ from the video summary")
        if event_counts["input"] != consumed:
            fail("timeline input events differ from the input summary")
        for kind, summary_name in (
            ("music", "music_calls"),
            ("voice", "voice_calls"),
            ("stop_music", "stop_music_calls"),
            ("stop_audio", "stop_audio_calls"),
        ):
            if event_counts[kind] != audio_counts[summary_name]:
                fail(f"timeline {kind} events differ from the audio summary")

        digest(data.get("state_fnv1a64"), "state_fnv1a64")
        digest(data.get("mapz_fnv1a64"), "mapz_fnv1a64", nullable=True)
        digest(data.get("name_fnv1a64"), "name_fnv1a64")

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
