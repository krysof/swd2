#!/usr/bin/env python3
"""Replay and lock direct item 192's pose-0 damage dispatcher path."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "item_pose0", "darkened_item_pose0", "effect_first", "effect_middle",
    "effect_last", "first_result_reaction", "result_number_first",
    "result_number_middle", "result_number_last", "bare_player_tail",
)
RESTORATION_FRAMES = tuple(range(55, 62))
RESTORATION_ANCHORS = ((59, 765), (60, 767), (61, 769))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item-damage digest {label}")
    return value


def crop_rgb(rgb: bytes, box: object) -> bytes:
    if box != [0, 0, 320, 197]:
        raise ValueError("item-damage restoration crop differs")
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
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                "original_fig_direct_item_damage_dispatcher" or \
                expected.get("formation_directory_offset") != 0x4D4 or \
                (expected.get("item_id"), expected.get("dispatcher_index"),
                 expected.get("canonical_ability_id"),
                 expected.get("effect_code")) != (192, 52, 52, 0x33) or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 15:
            raise ValueError("unsupported FIG item-damage reference")
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "straddle live VGA DAC writes" not in limitation or \
                "top 197 scanlines" not in limitation or \
                "bottom-two-scanline" not in limitation:
            raise ValueError("FIG item-damage evidence boundary differs")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the item-damage reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_sha256"]:
            raise ValueError("FIG item-damage inputs differ")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG item-damage staged save differs")

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
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("delay_milliseconds") != \
                expected["rewrite_delay_milliseconds"] or \
                trace.get("audio") != expected["rewrite_audio"]:
            raise ValueError("FIG item-damage timeline differs")
        if trace.get("frame_fnv1a64", [])[-1:] != [
                expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG item-damage state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG item-damage did not use IF entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG item-damage frame count differs")
        pages = expected.get("matched_frames")
        if not isinstance(pages, list) or tuple(
                page.get("kind") for page in pages) != EXPECTED_KINDS:
            raise ValueError("FIG item-damage page set differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG item-damage {page['kind']} differs from original")
            digest(page.get("original_png_sha256"),
                   f"{page.get('kind')}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("FIG item-damage review frame differs")

        restoration = expected.get("restoration_frames")
        if not isinstance(restoration, list) or tuple(
                page.get("rewrite_frame") for page in restoration) != \
                RESTORATION_FRAMES:
            raise ValueError("FIG item-damage restoration set differs")
        for page in restoration:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["indexed_sha256"] or \
                    sha256(palette) != page["palette_sha256"] or \
                    sha256(rgb) != page["rgb_sha256"]:
                raise ValueError("FIG item-damage restoration differs")

        anchors = expected.get("restoration_anchors")
        if not isinstance(anchors, list) or tuple(
                (page.get("rewrite_frame"),
                 page.get("original_review_frame")) for page in anchors) != \
                RESTORATION_ANCHORS:
            raise ValueError("FIG item-damage restoration anchors differ")
        for page in anchors:
            if page.get("mismatched_full_rgb_pixels") != 8:
                raise ValueError("FIG item-damage scanline evidence differs")
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            cropped = crop_rgb(rgb, page.get("crop"))
            if sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(cropped) != page["rewrite_crop_rgb_sha256"] or \
                    sha256(cropped) != page["original_crop_rgb_sha256"] or \
                    page["original_rgb_sha256"] == page["rewrite_rgb_sha256"]:
                raise ValueError("FIG item-damage restored anchor differs")
            digest(page.get("original_png_sha256"),
                   "restoration/original_png_sha256")
            digest(page.get("original_rgb_sha256"),
                   "restoration/original_rgb_sha256")

        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        print(
            "FIG direct item-damage checkpoint: item 192 keeps pose0 through "
            "effect 33h, reaction, ten result pages, and restoration")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item-damage checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
