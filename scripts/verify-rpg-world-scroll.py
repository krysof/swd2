#!/usr/bin/env python3
"""Replay and lock AREA1 original four-cell viewport scroll routes."""

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


ROUTES = {
    "original_rpg_area1_east_scroll_sequence": {
        "direction": "RIGHT",
        "initial_world": [126, 13],
        "final_world": [130, 13],
        "initial_viewport": [106, 1],
        "final_viewport": [110, 1],
        "kinds": ("initial_release_area1_world_page",
                  "second_east_scroll_page", "third_east_scroll_page"),
        "rewrite_frames": (0, 2, 3),
        "review_frames": (299, 535, 768),
        "matched_count": 3,
    },
    "original_rpg_area1_south_scroll_sequence": {
        "direction": "DOWN",
        "initial_world": [126, 13],
        "final_world": [126, 17],
        "initial_viewport": [106, 1],
        "final_viewport": [106, 5],
        "kinds": ("initial_release_area1_world_page",
                  "second_south_scroll_page"),
        "rewrite_frames": (0, 2),
        "review_frames": (299, 768),
        "matched_count": 2,
    },
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed AREA1 scroll digest {label}")


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
        route = ROUTES.get(expected.get("kind"))
        if expected.get("schema_version") != 1 or route is None or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("initial_world_position") != route["initial_world"] or \
                expected.get("requested_direction") != route["direction"] or \
                expected.get("direction_polls") != 4 or \
                expected.get("final_world_position") != route["final_world"] or \
                expected.get("initial_viewport_position") != \
                    route["initial_viewport"] or \
                expected.get("final_viewport_position") != \
                    route["final_viewport"] or \
                not isinstance(pages, list) or \
                len(pages) != route["matched_count"] or \
                tuple(page.get("kind") for page in pages) != route["kinds"] or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    route["rewrite_frames"] or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    route["review_frames"]:
            raise ValueError("unsupported RPG AREA1 scroll reference")
        if sha256((args.game / "RPG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("RPG.EXE differs from scroll reference")
        if tuple(sha256((args.game / "T1" / name).read_bytes()) for name in
                 ("AREA1.RAP", "AREA1.RSK", "AREA1.RRO")) != (
                    expected["area_layout_sha256"],
                    expected["area_graphics_metadata_sha256"],
                    expected["area_overlay_sha256"]):
            raise ValueError("AREA1 resources differ from scroll reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.save_root / "SAVE.DAQ").read_bytes()) != \
                    expected["fixture_save_sha256"] or \
                sha256((args.save_root / "MAPZ.DA1").read_bytes()) != \
                    MAPZ_SHA256 or \
                sha256((args.save_root / "MAPZ.DAQ").read_bytes()) != \
                    MAPZ_SHA256 or \
                sha256((args.save_root / "NAME1.DSK").read_bytes()) != \
                    NAME_SHA256 or \
                sha256((args.save_root / "NAMEQ.DSK").read_bytes()) != \
                    NAME_SHA256:
            raise ValueError("RPG AREA1 scroll save triplets differ")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("RPG AREA1 scroll input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 15 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_video_frames") != 1051 or \
                expected.get("capture_review_frames") != 1050 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != HARNESS_SHA256:
            raise ValueError("RPG AREA1 scroll capture boundary differs")
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
            raise ValueError("RPG AREA1 scroll replay timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("RPG AREA1 scroll final state differs")
        if trace.get("transitions") != [{
                "module": "RPG.EXE", "input": "OC", "output": "--",
                "launched": True}]:
            raise ValueError("RPG AREA1 scroll replay missed OC entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 5:
            raise ValueError("RPG AREA1 scroll frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "RPG AREA1 scroll page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        print(
            "RPG AREA1 scroll: released world (126,13), four "
            f"{route['direction'].lower()} viewport steps to "
            f"({route['final_world'][0]},{route['final_world'][1]}), "
            f"{route['matched_count']} full RGB checkpoints match the "
            "untouched original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG AREA1 scroll: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
