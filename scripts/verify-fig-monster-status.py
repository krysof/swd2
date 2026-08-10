#!/usr/bin/env python3
"""Replay and lock FIG's five original persistent monster status icons."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


ABILITY_IDS = (6, 72, 93, 56, 58)
EFFECT_CODES = (0x5E, 0x5F, 0x60, 0x64, 0x65)
ICON_FRAMES = (0xA1, 0xA4, 0x9E, 0xA5, 0x9F)
FRONTEND_BOUNDARIES = (84, 88, 119, 108, 132)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed monster-status evidence digest {label}")
    return value


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
        cases = expected.get("cases")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_monster_status_variants" or \
                expected.get("formation_directory_offset") != 1236 or \
                not isinstance(cases, list) or \
                [case.get("ability_id") for case in cases] != list(ABILITY_IDS) or \
                [case.get("effect_code") for case in cases] != list(EFFECT_CODES) or \
                [case.get("status_slot") for case in cases] != list(range(5)) or \
                [case.get("menu_icon_frame") for case in cases] != list(ICON_FRAMES):
            raise ValueError("unsupported FIG monster-status reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the monster-status reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original monster-status capture input differs")

        replay = args.reference.with_name("replay-fig-monster-status.txt")
        for case_index, case in enumerate(cases):
            ability_id = case["ability_id"]
            save_root = args.save_root / f"ability-{ability_id}"
            if sha256((save_root / "SAVE.DA1").read_bytes()) != \
                    case["fixture_save_sha256"]:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} staged save differs")
            case_output = args.output / f"ability-{ability_id}"
            case_output.mkdir(parents=True, exist_ok=True)
            trace_path = case_output / "trace.json"
            frame_path = case_output / "frames.bin"
            subprocess.run([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(save_root), "--slot", "1", "--no-save",
                "--start-marker", "IF", "--run-replay", str(replay),
                "--trace-output", str(trace_path),
                "--frame-output", str(frame_path),
            ], check=True, stdout=subprocess.DEVNULL)
            trace = json.loads(trace_path.read_text(encoding="utf-8"))
            if trace.get("input") != {
                    "total": 4, "consumed": 4, "remaining": 0,
                    "implicit_quit_calls": 0} or trace.get("boundaries") != {
                        "wait": 4, "poll": 0, "text": 0,
                        "frontend": FRONTEND_BOUNDARIES[case_index]}:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} input differs")
            if trace.get("video") != case["rewrite_video"] or \
                    trace.get("frame_fnv1a64", [])[-1:] != [
                        case["rewrite_final_fnv1a64"]]:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} video differs")
            if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                    trace.get("name_fnv1a64")) != (
                        case["rewrite_state_fnv1a64"],
                        "827f0f1b725a0958", "e3d2853e2676513b"):
                raise ValueError(
                    f"FIG monster-status ability {ability_id} state differs")
            if trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} did not use IF entry")

            frames = load_indexed_frames(frame_path)
            frame_index = case["rewrite_frame"]
            if len(frames) != case["rewrite_video"]["frames"] or \
                    frame_index != len(frames) - 1:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} frame index differs")
            pixels, palette = frames[frame_index]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != case["rewrite_indexed_sha256"] or \
                    sha256(palette) != case["rewrite_palette_sha256"] or \
                    sha256(rgb) != case["rewrite_rgb_sha256"] or \
                    sha256(rgb) != case["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} no longer matches original")
            for name in ("capture_video_sha256", "capture_manifest_sha256",
                         "original_png_sha256"):
                digest(case.get(name), f"{ability_id}/{name}")
            if not isinstance(case.get("original_review_frame"), int) or \
                    case["original_review_frame"] <= 0:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} review frame differs")
        digest(expected.get("capture_harness_sha256"), "capture_harness_sha256")
        print(
            "FIG monster-status checkpoint: all five 2deb MENU icon pages "
            "exactly match original 320x200 RGB frames")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG monster-status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
