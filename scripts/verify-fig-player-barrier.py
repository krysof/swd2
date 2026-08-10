#!/usr/bin/env python3
"""Replay and lock FIG selector 62h's active player-barrier handler."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_PAGES = (
    "common_pose0", "common_pose4", "darkened_pose4",
    "ordinary_target_card", "sp338_frame0", "sp338_frame1",
    "sp338_frame2", "sp338_frame3",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            c not in "0123456789abcdef" for c in value):
        raise ValueError(f"malformed player-barrier evidence digest {label}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_player_barrier" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("ability_id") != 64 or \
                expected.get("effect_code") != 0x62 or \
                expected.get("capture_time_limit_seconds") != 15:
            raise ValueError("unsupported FIG player-barrier reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the player-barrier reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original player-barrier capture input differs")
        if sha256((args.save_dir / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG player-barrier staged save differs")
        if not isinstance(expected.get("capture_limitation"), str) or \
                "585e/4417/0d98" not in expected["capture_limitation"]:
            raise ValueError("FIG player-barrier evidence boundary differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        replay = args.reference.with_name(expected["replay"])
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_dir), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"]:
            raise ValueError("FIG player-barrier input differs")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG player-barrier video differs")
        if trace.get("delay_milliseconds") != \
                expected["rewrite_delay_milliseconds"] or \
                trace.get("audio") != expected["rewrite_audio"]:
            raise ValueError("FIG player-barrier timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG player-barrier final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG player-barrier did not use IF entry")

        frames = load_indexed_frames(frame_path)
        pages = expected.get("matched_frames")
        if len(frames) != expected["rewrite_video"]["frames"] or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_PAGES:
            raise ValueError("FIG player-barrier reference page set differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG player-barrier {page['kind']} no longer matches original")
            digest(page.get("original_png_sha256"),
                   f"{page['kind']}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("FIG player-barrier review frame differs")
        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        print(
            "FIG player-barrier checkpoint: 43ce fade, ordinary target card, "
            "SP338 frames, deferred debit and 0d98 tail match original evidence")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG player-barrier checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
