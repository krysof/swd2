#!/usr/bin/env python3
"""Replay FIG's zero-selector damage handler and lock all 144e pages."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "common_pose0", "common_pose4", "darkened_pose4", "effect_first",
    "effect_middle", "effect_last", "reaction_without_number",
    *(f"result_page_{index}" for index in range(1, 11)),
    "restore_middle", "restore_late", "restored_pose4", "round_clean",
    "command_menu",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed implicit-damage digest {label}")


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
        if expected.get("schema_version") != 1 or expected.get("kind") != \
                "original_fig_implicit_monster_damage_dispatcher" or \
                expected.get("formation_directory_offset") != 0x4D4 or \
                (expected.get("ability_id"), expected.get("effect_code"),
                 expected.get("canonical_ability_id")) != (100, 0x54, 22):
            raise ValueError("unsupported FIG implicit-damage reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from implicit-damage reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original implicit-damage capture input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG implicit-damage staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay",
            str(args.reference.with_name("replay-fig-player-damage.txt")),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG implicit-damage replay differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG implicit-damage state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG implicit-damage did not use IF entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG implicit-damage frame count differs")
        matched = expected.get("matched_frames")
        if not isinstance(matched, list) or \
                tuple(page.get("kind") for page in matched) != EXPECTED_KINDS:
            raise ValueError("FIG implicit-damage page set differs")
        for page in matched:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG implicit-damage {page['kind']} differs from original")
            digest(page.get("original_png_sha256"),
                   f"{page.get('kind')}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("FIG implicit-damage review frame differs")
        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        print(
            "FIG implicit-damage checkpoint: ability 100 keeps the caster "
            "anchor, damages monster zero, and exactly matches reaction, "
            "ten cycling 144e pages, restore, clean, and command RGB")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG implicit-damage checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
