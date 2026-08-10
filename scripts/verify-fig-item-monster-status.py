#!/usr/bin/env python3
"""Replay and lock direct item 205's applied/resisted monster-status paths."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


CASES = (
    ("applied", 1236, "status_applied"),
    ("resisted", 392, "resisted"),
)
PAGE_KINDS = {
    "applied": (
        "item_pose0", "darkened_item_pose0", "effect_first",
        "effect_last", "bare_player_tail",
    ),
    "resisted": (
        "item_pose0", "darkened_item_pose0", "effect_first",
        "effect_last", "immunity_card", "bare_player_tail",
    ),
}
RESTORATION_COUNTS = {"applied": 7, "resisted": 6}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item monster-status digest {label}")
    return value


def crop_rgb(rgb: bytes, box: object) -> bytes:
    if box != [0, 0, 320, 197]:
        raise ValueError("item monster-status restoration crop differs")
    return rgb[:320 * 197 * 3]


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
                "original_fig_direct_item_monster_status" or \
                (expected.get("item_id"), expected.get("dispatcher_index"),
                 expected.get("canonical_ability_id"),
                 expected.get("effect_code")) != (205, 65, 6, 0x5E) or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 15 or \
                not isinstance(cases, list) or \
                [(case.get("name"),
                  case.get("formation_directory_offset"),
                  case.get("outcome")) for case in cases] != list(CASES):
            raise ValueError("unsupported FIG item monster-status reference")
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "bottom two scanlines" not in limitation or \
                "top 197 scanlines" not in limitation:
            raise ValueError("FIG item monster-status evidence boundary differs")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the item-status reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_sha256"]:
            raise ValueError("FIG item monster-status inputs differ")
        digest(expected.get("capture_harness_sha256"),
               "capture_harness_sha256")

        for case in cases:
            name = case["name"]
            save_dir = args.save_root / name
            if sha256((save_dir / "SAVE.DA1").read_bytes()) != \
                    case["fixture_save_sha256"]:
                raise ValueError(f"FIG item monster-status {name} save differs")

            case_output = args.output / name
            case_output.mkdir(parents=True, exist_ok=True)
            trace_path = case_output / "trace.json"
            frame_path = case_output / "frames.bin"
            subprocess.run([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(save_dir), "--slot", "1", "--no-save",
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
                raise ValueError(f"FIG item monster-status {name} timeline differs")
            if trace.get("frame_fnv1a64", [])[-1:] != [
                    case["rewrite_final_fnv1a64"]] or \
                    trace.get("state_fnv1a64") != \
                    case["rewrite_state_fnv1a64"] or \
                    trace.get("mapz_fnv1a64") != "827f0f1b725a0958" or \
                    trace.get("name_fnv1a64") != "e3d2853e2676513b":
                raise ValueError(f"FIG item monster-status {name} state differs")
            if trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
                raise ValueError(
                    f"FIG item monster-status {name} did not use IF entry")

            frames = load_indexed_frames(frame_path)
            if len(frames) != case["rewrite_video"]["frames"]:
                raise ValueError(
                    f"FIG item monster-status {name} frame count differs")
            pages = case.get("matched_frames")
            if not isinstance(pages, list) or tuple(
                    page.get("kind") for page in pages) != PAGE_KINDS[name]:
                raise ValueError(
                    f"FIG item monster-status {name} page set differs")
            for page in pages:
                pixels, palette = frames[page["rewrite_frame"]]
                rgb = expand_rgb(pixels, palette)
                if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                        sha256(palette) != page["rewrite_palette_sha256"] or \
                        sha256(rgb) != page["rewrite_rgb_sha256"] or \
                        sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG item monster-status {name}/{page['kind']} differs")
                digest(page.get("original_png_sha256"),
                       f"{name}/{page['kind']}.original_png_sha256")

            restoration = case.get("restoration_frames")
            if not isinstance(restoration, list) or \
                    len(restoration) != RESTORATION_COUNTS[name]:
                raise ValueError(
                    f"FIG item monster-status {name} restoration set differs")
            for page in restoration:
                pixels, palette = frames[page["rewrite_frame"]]
                if sha256(pixels) != page["indexed_sha256"] or \
                        sha256(palette) != page["palette_sha256"]:
                    raise ValueError(
                        f"FIG item monster-status {name} restoration differs")

            anchor = case.get("restored_anchor_crop")
            if not isinstance(anchor, dict) or \
                    anchor.get("mismatched_full_rgb_pixels") != 8:
                raise ValueError(
                    f"FIG item monster-status {name} anchor evidence differs")
            pixels, palette = frames[anchor["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            cropped = crop_rgb(rgb, anchor.get("crop"))
            if sha256(rgb) != anchor["rewrite_rgb_sha256"] or \
                    sha256(cropped) != anchor["rewrite_crop_rgb_sha256"] or \
                    sha256(cropped) != anchor["original_crop_rgb_sha256"] or \
                    anchor["original_rgb_sha256"] == \
                    anchor["rewrite_rgb_sha256"]:
                raise ValueError(
                    f"FIG item monster-status {name} restored anchor differs")
            digest(anchor.get("original_png_sha256"),
                   f"{name}/anchor.original_png_sha256")
            digest(anchor.get("original_rgb_sha256"),
                   f"{name}/anchor.original_rgb_sha256")
            for key in ("capture_video_sha256", "capture_manifest_sha256"):
                digest(case.get(key), f"{name}/{key}")

        print(
            "FIG direct item monster-status checkpoint: item 205 keeps pose0 "
            "through successful and resisted 5eh restoration paths")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item monster-status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
