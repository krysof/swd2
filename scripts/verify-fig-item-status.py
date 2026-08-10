#!/usr/bin/env python3
"""Replay and lock FIG direct-item status and flagged-medium paths."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


CASES = ((186, 46, 0x69, "status_applied"),
         (226, 86, 0x63, "missing_target_flag_medium"))
PAGE_KINDS = {
    186: ("item_pose0", "darkened_item_pose0", "dark_status_card",
          "restoration_step2", "restoration_step3", "restoration_step4",
          "restored_status_card", "bare_player_tail"),
    226: ("item_pose0", "darkened_item_pose0",
          "dark_missing_medium_card", "bare_player_tail"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed item-status evidence digest {label}")
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
                expected.get("kind") != \
                "original_fig_direct_item_status_paths" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 15 or \
                not isinstance(cases, list) or \
                [(case.get("item_id"), case.get("embedded_ability_id"),
                  case.get("effect_code"), case.get("outcome"))
                 for case in cases] != list(CASES):
            raise ValueError("unsupported FIG direct item-status reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the item-status reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_sha256"]:
            raise ValueError("FIG item-status capture/replay input differs")
        digest(expected.get("capture_harness_sha256"),
               "capture_harness_sha256")

        for case in cases:
            item_id = case["item_id"]
            save_root = args.save_root / f"item-{item_id}"
            if sha256((save_root / "SAVE.DA1").read_bytes()) != \
                    case["fixture_save_sha256"]:
                raise ValueError(f"FIG item {item_id} staged save differs")
            case_output = args.output / f"item-{item_id}"
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
                    trace.get("boundaries") != case["rewrite_boundaries"] or \
                    trace.get("video") != case["rewrite_video"] or \
                    trace.get("delay_milliseconds") != \
                    case["rewrite_delay_milliseconds"] or \
                    trace.get("audio") != case["rewrite_audio"]:
                raise ValueError(f"FIG item {item_id} timeline differs")
            if trace.get("frame_fnv1a64", [])[-1:] != [
                    case["rewrite_final_fnv1a64"]] or \
                    trace.get("state_fnv1a64") != \
                    case["rewrite_state_fnv1a64"] or \
                    trace.get("mapz_fnv1a64") != "827f0f1b725a0958" or \
                    trace.get("name_fnv1a64") != "e3d2853e2676513b":
                raise ValueError(f"FIG item {item_id} final state differs")
            if trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
                raise ValueError(f"FIG item {item_id} did not use IF entry")

            frames = load_indexed_frames(frame_path)
            if len(frames) != case["rewrite_video"]["frames"]:
                raise ValueError(f"FIG item {item_id} frame count differs")
            pages = case.get("matched_frames")
            if not isinstance(pages, list) or tuple(
                    page.get("kind") for page in pages) != PAGE_KINDS[item_id]:
                raise ValueError(f"FIG item {item_id} page set differs")
            for page in pages:
                pixels, palette = frames[page["rewrite_frame"]]
                rgb = expand_rgb(pixels, palette)
                if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                        sha256(palette) != page["rewrite_palette_sha256"] or \
                        sha256(rgb) != page["rewrite_rgb_sha256"] or \
                        sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG item {item_id}/{page['kind']} no longer matches")
                digest(page.get("original_png_sha256"),
                       f"{item_id}/{page['kind']}.original_png_sha256")
            restoration = case.get("restoration_frames")
            if not isinstance(restoration, list) or not restoration:
                raise ValueError(f"FIG item {item_id} lacks restoration pages")
            for page in restoration:
                pixels, palette = frames[page["rewrite_frame"]]
                if sha256(pixels) != page["indexed_sha256"] or \
                        sha256(palette) != page["palette_sha256"]:
                    raise ValueError(
                        f"FIG item {item_id} restoration page differs")
            for name in ("capture_video_sha256", "capture_manifest_sha256"):
                digest(case.get(name), f"{item_id}/{name}")

        print(
            "FIG direct item-status checkpoint: item 186's 69h status path "
            "and item 226's target-flag 63h missing-medium path match the "
            "original stable RGB pages and indexed restoration envelope")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG direct item-status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
