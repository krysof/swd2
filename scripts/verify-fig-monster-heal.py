#!/usr/bin/env python3
"""Replay and lock FIG's original low-HP monster self-heal sequence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_FRAMES = (23, 24, 25, 26, 27, 28)
PAGE_LABELS = (
    "player damage result retained before self-heal turn",
    "player cleanup retains the acting pose",
    "monster-turn bare preparation page",
    "self-heal name card on the bare battlefield",
    "self-heal clean tail without a floating number",
    "following player pose reinstalls the party card",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed monster-heal evidence digest {label}")
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
                    "original_fig_monster_self_heal_sequence" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("formation_directory_offset") != 288 or \
                reference.get("monster_definition_id") != 368 or \
                reference.get("healing_ability_id") != 50 or \
                reference.get("healing_value") != 52 or \
                not isinstance(pages, list) or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_FRAMES or \
                tuple(page.get("label") for page in pages) != PAGE_LABELS:
            raise ValueError("unsupported FIG monster-heal reference")

        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from monster-heal reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("monster-heal staged SAVE differs")
        autotype = args.reference.with_name(reference["capture_autotype"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"]:
            raise ValueError("monster-heal original AUTOTYPE differs")
        if reference.get("capture_wait_seconds") != 5 or \
                reference.get("capture_pace_seconds") != 1 or \
                reference.get("capture_time_limit_seconds") != 30 or \
                reference.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("monster-heal capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        replay = args.reference.with_name("replay-fig-monster-heal.txt")
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
                    "wait": 3, "poll": 0, "text": 0, "frontend": 267}:
            raise ValueError("monster-heal replay input boundaries differ")
        if trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"]:
            raise ValueError("monster-heal video/audio timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("monster-heal final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("monster-heal replay did not use the IF entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("monster-heal frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "monster-heal page no longer matches original: "
                    f"{page['label']}")
            digest(page.get("original_png_sha256"),
                   f"{page['label']}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int) or \
                    page["original_review_frame"] <= 0:
                raise ValueError(
                    f"monster-heal review frame differs: {page['label']}")

        print(
            "FIG monster-heal checkpoint: player tail, bare preparation, "
            "name-only self-heal, bare cleanup and following pose exactly "
            "match original 320x200 RGB")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG monster-heal checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
