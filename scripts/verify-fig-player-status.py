#!/usr/bin/env python3
"""Replay and lock FIG's four original learned-support status pages."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


ABILITY_IDS = (35, 38, 33, 37)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            c not in "0123456789abcdef" for c in value):
        raise ValueError(f"malformed player-status evidence digest {label}")
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
                expected.get("kind") != "original_fig_player_status_variants" or \
                expected.get("formation_directory_offset") != 392 or \
                not isinstance(cases, list) or \
                [case.get("ability_id") for case in cases] != list(ABILITY_IDS):
            raise ValueError("unsupported FIG player-status reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the player-status reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original player-status capture input differs")

        replay = args.reference.with_name("replay-fig-player-status.txt")
        for case in cases:
            ability_id = case["ability_id"]
            save_root = args.save_root / f"ability-{ability_id}"
            if sha256((save_root / "SAVE.DA1").read_bytes()) != \
                    case["fixture_save_sha256"]:
                raise ValueError(
                    f"FIG player-status ability {ability_id} staged save differs")
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
            if trace.get("input") != case["rewrite_input"] or \
                    trace.get("boundaries") != case["rewrite_boundaries"]:
                raise ValueError(
                    f"FIG player-status ability {ability_id} input differs")
            if trace.get("video") != case["rewrite_video"] or \
                    trace.get("frame_fnv1a64", [])[-1:] != [
                        case["rewrite_final_fnv1a64"]]:
                raise ValueError(
                    f"FIG player-status ability {ability_id} video differs")
            if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                    trace.get("name_fnv1a64")) != (
                        case["rewrite_state_fnv1a64"],
                        "827f0f1b725a0958", "e3d2853e2676513b"):
                raise ValueError(
                    f"FIG player-status ability {ability_id} final state differs")
            if trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
                raise ValueError(
                    f"FIG player-status ability {ability_id} did not use IF entry")

            frames = load_indexed_frames(frame_path)
            if len(frames) != case["rewrite_video"]["frames"]:
                raise ValueError(
                    f"FIG player-status ability {ability_id} frame count differs")
            matched = case.get("matched_frames")
            if not isinstance(matched, list) or len(matched) != 3:
                raise ValueError("FIG player-status reference page set differs")
            for page in matched:
                pixels, palette = frames[page["rewrite_frame"]]
                rgb = expand_rgb(pixels, palette)
                if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                        sha256(palette) != page["rewrite_palette_sha256"] or \
                        sha256(rgb) != page["rewrite_rgb_sha256"] or \
                        sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG {ability_id}/{page['kind']} no longer matches original")
                digest(page.get("original_png_sha256"),
                       f"{ability_id}/{page['kind']}.original_png_sha256")
            for name in ("capture_video_sha256", "capture_manifest_sha256"):
                digest(case.get(name), f"{ability_id}/{name}")
        digest(expected.get("capture_harness_sha256"), "capture_harness_sha256")
        print(
            "FIG player-status checkpoint: abilities 35/38/33/37, twelve full "
            "pose0/pose4/status 320x200 RGB pages exactly match the original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG player-status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
