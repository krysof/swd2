#!/usr/bin/env python3
"""Lock FIG 2485's living-slot card mask through generic result pages."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed generic dead-slot digest {label}")
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
        pages = reference.get("matched_frames")
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != \
                    "original_fig_monster_generic_dead_slot_mask" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                tuple(reference.get(name) for name in (
                    "formation_directory_offset", "monster_definition_id",
                    "ability_id", "effect_code", "target_flags")) != \
                    (734, 334, 74, 0x42, 0x8401) or \
                reference.get("initial_living_party_slots") != [0, 2] or \
                reference.get("original_matched_rewrite_frames") != \
                    list(range(1, 38)) or \
                not isinstance(pages, list) or len(pages) != 37 or \
                [page.get("rewrite_frame") for page in pages] != \
                    list(range(1, 38)):
            raise ValueError("unsupported FIG generic dead-slot reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"] or \
                sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("generic dead-slot program or fixture differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("generic dead-slot input evidence differs")
        if (reference.get("capture_harness_sha256"),
                reference.get("capture_wait_seconds"),
                reference.get("capture_pace_seconds"),
                reference.get("capture_time_limit_seconds")) != (
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba",
                    5, 1, 20):
            raise ValueError("generic dead-slot capture boundary differs")

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
        if trace.get("input") != reference["rewrite_input"] or \
                trace.get("boundaries") != reference["rewrite_boundaries"] or \
                trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    reference["rewrite_mapz_fnv1a64"],
                    reference["rewrite_name_fnv1a64"]):
            raise ValueError("generic dead-slot deterministic trace differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "OC",
                "launched": True}]:
            raise ValueError("generic dead-slot replay missed victory boundary")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 38:
            raise ValueError("generic dead-slot frame boundary differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "generic dead-slot page differs from original: " +
                    str(page["rewrite_frame"]))
            digest(page.get("original_png_sha256"),
                   f"frame {page['rewrite_frame']}/original_png_sha256")
        print(
            "FIG generic dead-slot checkpoint: 2485 keeps only physical "
            "slots zero and two across all 37 exact original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG generic dead-slot checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
