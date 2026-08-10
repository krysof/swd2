#!/usr/bin/env python3
"""Replay and lock FIG's zero periodic-damage 0b75/144e page sequence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_KINDS = (
    "bare_prepare", "reaction",
    *(f"result_page_{index}" for index in range(1, 11)),
    "bare_clean",
)
PAGE_INDICES = (34, 35, *range(36, 46), 46)
EXACT_ORIGINAL_KINDS = (
    "bare_prepare", "reaction",
    *(f"result_page_{index}" for index in range(3, 11)),
    "bare_clean",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed periodic-zero digest {label}")


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
                expected.get("kind") != "original_fig_periodic_zero_damage" or \
                expected.get("formation_directory_offset") != 1240 or \
                (expected.get("ability_id"), expected.get("effect_code")) != \
                (48, 0x60) or expected.get("random_cursor") != 0x1004:
            raise ValueError("unsupported FIG periodic-zero reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the periodic-zero reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"]:
            raise ValueError("original periodic-zero capture input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG periodic-zero staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay",
            str(args.reference.with_name("replay-fig-periodic-zero.txt")),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG periodic-zero replay differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG periodic-zero state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG periodic-zero did not use IF entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG periodic-zero frame count differs")
        pages = expected.get("full_modern_frames")
        if not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != PAGE_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                PAGE_INDICES:
            raise ValueError("FIG periodic-zero page set differs")
        exact_kinds = []
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"]:
                raise ValueError(
                    f"FIG periodic-zero {page['kind']} modern page differs")
            if "original_rgb_sha256" in page:
                exact_kinds.append(page["kind"])
                if sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG periodic-zero {page['kind']} differs from original")
                digest(page.get("original_png_sha256"),
                       f"{page['kind']}/original_png_sha256")
                if not isinstance(page.get("original_review_frame"), int):
                    raise ValueError("periodic-zero review frame differs")
        if tuple(exact_kinds) != EXACT_ORIGINAL_KINDS or \
                expected.get("unobserved_stable_original_pages") != [
                    "result_page_1", "result_page_2"]:
            raise ValueError("FIG periodic-zero original evidence set differs")
        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        print(
            "FIG periodic-zero checkpoint: bare 2db8 preparation, retained "
            "reaction, ten literal-zero pages, E0h..E4h cycling, and bare "
            "four-tick clean tail match original stable RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG periodic-zero checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
