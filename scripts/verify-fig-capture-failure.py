#!/usr/bin/env python3
"""Replay and lock FIG's random-encounter capture-failure sequence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


SEQUENCE_FRAMES = tuple(range(5, 32))
SEQUENCE_TIMES = (
    0, 0, 257, 328, 399, 456, 456, 470, 484, 498, 512, 526,
    540, 554, 568, 582, 596, 610, 624, 638, 652, 666, 680, 694,
    779, 822, 902,
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed FIG capture-failure digest {label}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        sequence = reference.get("sequence")
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != \
                    "original_fig_random_capture_failure_sequence" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("random_directory_base") != 100 or \
                reference.get("redirected_formation_directory_offset") != 288 or \
                reference.get("monster_definition_id") != 368 or \
                not isinstance(sequence, list) or \
                tuple(page.get("rewrite_frame") for page in sequence) != \
                    SEQUENCE_FRAMES:
            raise ValueError("unsupported FIG capture-failure reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from capture-failure reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("FIG capture-failure staged SAVE differs")
        if sha256((args.game / "ORC.EXE").read_bytes()) != \
                reference["fixture_orc_sha256"]:
            raise ValueError("FIG capture-failure staged ORC differs")
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"]:
            raise ValueError("FIG capture-failure original AUTOTYPE differs")
        if sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("FIG capture-failure replay input differs")
        if reference.get("capture_wait_seconds") != 5 or \
                reference.get("capture_pace_seconds") != 1 or \
                reference.get("capture_time_limit_seconds") != 15 or \
                reference.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG capture-failure capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != {
                "total": 5, "consumed": 5, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 5, "poll": 0, "text": 0, "frontend": 84}:
            raise ValueError("FIG capture-failure replay boundaries differ")
        if trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"]:
            raise ValueError("FIG capture-failure timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG capture-failure final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG capture-failure replay missed IF entry")

        timeline = trace.get("timeline", [])
        frame_times = {
            item["frame"]: item["at_milliseconds"]
            for item in timeline if item.get("kind") == "frame"
        }
        if tuple(frame_times.get(index) for index in SEQUENCE_FRAMES) != \
                SEQUENCE_TIMES:
            raise ValueError("FIG capture-failure frame timing differs")
        voices = [item for item in timeline if item.get("kind") == "voice"]
        checks = reference.get("voice_checkpoints")
        if not isinstance(checks, list) or len(checks) != 2 or len(voices) != 2:
            raise ValueError("FIG capture-failure voice count differs")
        for voice, expected in zip(voices, checks):
            if (voice.get("call"), voice.get("at_milliseconds"),
                    voice.get("payload_fnv1a64")) != (
                        expected.get("call"), expected.get("at_milliseconds"),
                        expected.get("payload_fnv1a64")) or \
                    expected.get("resource") not in ("SP016.VOC", "SP106.VOC"):
                raise ValueError("FIG capture-failure voice checkpoint differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("FIG capture-failure frame count differs")
        for page in sequence:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rgb_sha256"]:
                raise ValueError(
                    "FIG capture-failure exact sequence differs at frame " +
                    str(page["rewrite_frame"]))
            digest(page.get("original_png_sha256"),
                   "frame/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int) or \
                    page["original_review_frame"] <= 0:
                raise ValueError("invalid original capture-failure review frame")

        print(
            "FIG capture-failure checkpoint: target-anchored pot/failure cards, "
            "18/5-tick holds, 0d98 bare boundary and following enemy turn all "
            "match original RGB")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG capture-failure checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
