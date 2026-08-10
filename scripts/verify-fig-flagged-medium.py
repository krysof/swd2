#!/usr/bin/env python3
"""Replay and lock FIG's target-flag player missing-medium path."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def valid_digest(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        c in "0123456789abcdef" for c in value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_flagged_medium" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("ability_id") != 86 or \
                expected.get("effect_code") != 0x63:
            raise ValueError("unsupported FIG flagged-medium reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the flagged-medium reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original flagged-medium capture input differs")
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("flagged-medium rewrite input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG flagged-medium staged save differs")
        if (expected.get("capture_wait_seconds"),
                expected.get("capture_pace_seconds"),
                expected.get("capture_time_limit_seconds")) != (5, 1, 15):
            raise ValueError("FIG flagged-medium capture boundary differs")

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
                "total": 4, "consumed": 4, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 4, "poll": 0, "text": 0, "frontend": 81}:
            raise ValueError("FIG flagged-medium input boundaries differ")
        if trace.get("video") != {
                "frames": 31, "direct_updates": 0,
                "last_width": 320, "last_height": 200,
                "fnv1a64": expected["rewrite_video_fnv1a64"]} or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG flagged-medium video differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG flagged-medium final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG flagged-medium did not use direct IF entry")

        frames = load_indexed_frames(frame_path)
        matched = expected.get("matched_frames")
        if len(frames) != 31 or not isinstance(matched, list) or \
                tuple(page.get("kind") for page in matched) != (
                    "common_pose0", "common_pose4", "paid_missing_medium",
                    "player_action_boundary"):
            raise ValueError("FIG flagged-medium frame set differs")
        for page in matched:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG {page['kind']} no longer exactly matches original RGB")
            if not valid_digest(page.get("original_png_sha256")):
                raise ValueError("malformed flagged-medium original PNG digest")
        for name in (
                "capture_harness_sha256", "capture_video_sha256",
                "capture_manifest_sha256"):
            if not valid_digest(expected.get(name)):
                raise ValueError(f"malformed flagged-medium digest {name}")
        voices = [
            item for item in trace.get("timeline", [])
            if item.get("kind") == "voice"
        ]
        checks = expected.get("voice_checkpoints")
        if not isinstance(checks, list) or len(checks) != 2 or \
                len(voices) != 2:
            raise ValueError("FIG flagged-medium voice count differs")
        for voice, check in zip(voices, checks):
            if (voice.get("call"), voice.get("at_milliseconds"),
                    voice.get("payload_fnv1a64")) != (
                        check.get("call"), check.get("at_milliseconds"),
                        check.get("payload_fnv1a64")):
                raise ValueError("FIG flagged-medium voice timing differs")
        print(
            "FIG flagged-medium checkpoint: paid pose0/pose4, 58fa card and "
            "0d98 bare boundary match complete original 320x200 RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG flagged-medium checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
