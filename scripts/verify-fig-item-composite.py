#!/usr/bin/env python3
"""Replay and lock type-10 item 230's composite phases and AP return."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_PAGES = (
    "item_pose0", "first_nested_effect", "first_nested_result",
    "player_action_boundary", "post_debit_next_command",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed composite-item digest {label}")
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
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_type10_composite_item" or \
                expected.get("formation_directory_offset") != 0x4D4 or \
                (expected.get("item_id"), expected.get("canonical_ability_id"),
                 expected.get("effect_code"), expected.get("nested_effects"),
                 expected.get("resource_cost"), expected.get("payment_pool")) != \
                (230, 90, 0x6B, [0x38, 0x3A], 40, "ability_points") or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 15:
            raise ValueError("unsupported FIG composite-item reference")
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "top 197 scanlines" not in limitation or \
                "eight bottom-two-scanline pixels" not in limitation:
            raise ValueError("FIG composite-item capture boundary differs")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"] or \
                sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError("FIG/ITEM data differs from composite-item reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_sha256"]:
            raise ValueError("FIG composite-item inputs differ")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG composite-item staged save differs")

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
            raise ValueError("FIG composite-item timeline differs")
        if trace.get("frame_fnv1a64", [])[-1:] != [
                expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG composite-item final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG composite-item did not use IF entry")

        frames = load_indexed_frames(frame_path)
        pages = expected.get("matched_frames")
        if len(frames) != expected["rewrite_video"]["frames"] or \
                not isinstance(pages, list) or tuple(
                    page.get("kind") for page in pages) != EXPECTED_PAGES:
            raise ValueError("FIG composite-item page set differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG composite-item {page['kind']} differs from original")
            digest(page.get("original_png_sha256"), "original_png_sha256")

        anchor = expected.get("second_nested_anchor")
        if not isinstance(anchor, dict) or anchor.get("rewrite_frame") != 55 or \
                anchor.get("original_review_frame") != 845 or \
                anchor.get("crop") != [0, 0, 320, 197] or \
                anchor.get("mismatched_full_rgb_pixels") != 8:
            raise ValueError("FIG composite second-phase anchor differs")
        pixels, palette = frames[anchor["rewrite_frame"]]
        rgb = expand_rgb(pixels, palette)
        cropped = rgb[:320 * 197 * 3]
        if sha256(pixels) != anchor["rewrite_indexed_sha256"] or \
                sha256(palette) != anchor["rewrite_palette_sha256"] or \
                sha256(rgb) != anchor["rewrite_rgb_sha256"] or \
                sha256(cropped) != anchor["rewrite_crop_rgb_sha256"] or \
                sha256(cropped) != anchor["original_crop_rgb_sha256"] or \
                anchor["original_rgb_sha256"] == anchor["rewrite_rgb_sha256"]:
            raise ValueError("FIG composite second-phase crop differs")
        digest(anchor.get("original_png_sha256"), "anchor/original_png_sha256")
        digest(anchor.get("original_rgb_sha256"), "anchor/original_rgb_sha256")
        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        print(
            "FIG composite-item checkpoint: item 230 keeps both 38h/3ah "
            "phases and exposes the 40-point AP debit at the next command")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG composite-item checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
