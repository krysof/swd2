#!/usr/bin/env python3
"""Replay one shipped FIG generic-monster 25ee flash colour class."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_FRAMES = (3, 4, 6, 7, 14, 15)
PAGE_LABELS = (
    "enemy-turn bare preparation page",
    "generic ability name on the bare battlefield",
    "25ee clean party page",
    "25ee first solid-colour page",
    "25ee final clean party page",
    "following player first pose",
)
REFERENCE_PROGRAM_SHA256 = (
    "c4e0f0d04d25b70f80ab3d3490a81ef78c375193ccac70dad5b09c186c758876"
)
HARNESS_SHA256 = (
    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed generic-flash digest {label}")
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
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = reference.get("pages")
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != \
                    "original_fig_monster_generic_flash_class" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("variant") not in ("color81", "color5c") or \
                reference.get("reference_program_sha256") != \
                    REFERENCE_PROGRAM_SHA256 or \
                reference.get("capture_harness_sha256") != HARNESS_SHA256 or \
                not isinstance(pages, list) or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_FRAMES or \
                tuple(page.get("label") for page in pages) != PAGE_LABELS:
            raise ValueError("unsupported FIG generic-flash reference")
        expected_metadata = {
            "color81": (36, 362, 4, 0x5F, 0xA205, 0x81),
            "color5c": (548, 386, 70, 0x41, 0xA401, 0x5C),
        }[reference["variant"]]
        actual_metadata = tuple(reference.get(name) for name in (
            "formation_directory_offset", "monster_definition_id",
            "ability_id", "effect_code", "target_flags",
            "solid_palette_index"))
        if actual_metadata != expected_metadata:
            raise ValueError("generic-flash class metadata differs")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                REFERENCE_PROGRAM_SHA256 or \
                sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("generic-flash program or fixture differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("generic-flash input evidence differs")
        if (reference.get("capture_wait_seconds"),
                reference.get("capture_pace_seconds"),
                reference.get("capture_time_limit_seconds")) != (5, 1, 30):
            raise ValueError("generic-flash capture boundary differs")

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
                "total": 3, "consumed": 3, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 3, "poll": 0, "text": 0, "frontend": 551}:
            raise ValueError("generic-flash replay boundaries differ")
        for key in ("rewrite_video", "rewrite_audio"):
            if trace.get(key.removeprefix("rewrite_")) != reference[key]:
                raise ValueError(f"generic-flash {key} differs")
        if trace.get("delay_milliseconds") != \
                reference["rewrite_delay_milliseconds"] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("generic-flash deterministic state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("generic-flash replay missed IF entry")
        frame_times = {
            item["frame"]: item["at_milliseconds"]
            for item in trace.get("timeline", []) if item.get("kind") == "frame"
        }
        if tuple(frame_times.get(index) for index in range(6, 15)) != (
                384, 384, 439, 494, 549, 604, 659, 714, 769):
            raise ValueError("25ee one-tick alternation differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("generic-flash frame count differs")
        solid_pixels, _ = frames[7]
        if set(solid_pixels) != {reference["solid_palette_index"]}:
            raise ValueError("25ee solid page uses the wrong VGA index")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "generic-flash page differs from original: " +
                    page["label"])
            digest(page.get("original_png_sha256"),
                   page["label"] + "/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("generic-flash review-frame identity differs")
        print(
            f"FIG generic flash {reference['variant']}: six stable pages "
            f"match original RGB and 25ee alternates VGA index "
            f"{reference['solid_palette_index']:02x}h for eight one-tick steps")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG generic-flash checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
