#!/usr/bin/env python3
"""Replay and lock AREA1's original hard east-wall collision."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


MAPZ_SHA256 = "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917"
NAME_SHA256 = "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba"
HARNESS_SHA256 = "b884ff334ce081992422aa9a6b7e6a31685443c10869fa819d0fcb2a4e92b04b"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed AREA1 hard-block digest {label}")


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
        pages = expected.get("matched_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_rpg_area1_hard_block_sequence" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("initial_world_position") != [120, 12] or \
                expected.get("requested_direction") != "RIGHT" or \
                expected.get("repeated_direction_polls") != 8 or \
                expected.get("final_world_position") != [120, 12] or \
                expected.get("blocking_cells") != [
                    [122, 12], [121, 13], [121, 11]] or \
                expected.get("blocking_cell_words") != [
                    0x8013, 0x8383, 0x808E] or \
                not isinstance(pages, list) or len(pages) != 3 or \
                tuple(page.get("kind") for page in pages) != (
                    "initial_area1_hard_block_world_page",
                    "fourth_blocked_east_poll_page",
                    "eighth_blocked_east_poll_page") or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    (0, 4, 8) or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    (299, 531, 569):
            raise ValueError("unsupported RPG AREA1 hard-block reference")
        if sha256((args.game / "RPG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("RPG.EXE differs from hard-block reference")
        if tuple(sha256((args.game / "T1" / name).read_bytes()) for name in
                 ("AREA1.RAP", "AREA1.RSK", "AREA1.RRO")) != (
                    expected["area_layout_sha256"],
                    expected["area_graphics_metadata_sha256"],
                    expected["area_overlay_sha256"]):
            raise ValueError("AREA1 resources differ from hard-block reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("RPG AREA1 hard-block staged save differs")
        if sha256((args.save_root / "SAVE.DAQ").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.save_root / "MAPZ.DA1").read_bytes()) != \
                    MAPZ_SHA256 or \
                sha256((args.save_root / "MAPZ.DAQ").read_bytes()) != \
                    MAPZ_SHA256 or \
                sha256((args.save_root / "NAME1.DSK").read_bytes()) != \
                    NAME_SHA256 or \
                sha256((args.save_root / "NAMEQ.DSK").read_bytes()) != \
                    NAME_SHA256:
            raise ValueError("RPG AREA1 hard-block save triplets differ")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("RPG AREA1 hard-block input evidence differs")
        if expected.get("capture_wait_seconds") != 4.7 or \
                expected.get("capture_pace_seconds") != 0.23 or \
                expected.get("capture_time_limit_seconds") != 9 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_video_frames") != 630 or \
                expected.get("capture_review_frames") != 629 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != HARNESS_SHA256:
            raise ValueError("RPG AREA1 hard-block capture boundary differs")
        for name in ("area_layout_sha256", "area_graphics_metadata_sha256",
                     "area_overlay_sha256", "capture_harness_sha256",
                     "capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "OC", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"]:
            raise ValueError("RPG AREA1 hard-block replay timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("RPG AREA1 hard-block final state differs")
        if trace.get("transitions") != [{
                "module": "RPG.EXE", "input": "OC", "output": "--",
                "launched": True}]:
            raise ValueError("RPG AREA1 hard-block replay missed OC entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 9:
            raise ValueError("RPG AREA1 hard-block frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "RPG AREA1 hard-block page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        print(
            "RPG AREA1 hard block: eight east polls at world (120,12), "
            "8013h edge plus blocked 8383h/808eh corners, stationary "
            "facing/animation checkpoints match the untouched original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG AREA1 hard block: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
