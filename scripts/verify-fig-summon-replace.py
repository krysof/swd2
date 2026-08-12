#!/usr/bin/env python3
"""Replay and lock FIG's full captured-ally replacement selector."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed FIG summon-replacement digest {label}")


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
                expected.get("kind") != "original_fig_summon_replacement" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 100 or \
                expected.get("initial_captured_items") != [420, 419, 418] or \
                expected.get("installed_captured_items") != [420, 418] or \
                expected.get("replacement_item") != 419:
            raise ValueError("unsupported FIG summon-replacement reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from summon-replacement reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original summon-replacement input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG summon-replacement staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay",
            str(args.reference.with_name(expected["rewrite_replay"])),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                expected["rewrite_delay_milliseconds"]:
            raise ValueError("FIG summon-replacement replay boundaries differ")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG summon-replacement replay state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG summon-replacement replay did not use IF entry")
        if trace.get("frame_fnv1a64", [])[-1:] != [
                expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG summon-replacement final frame FNV differs")

        frames = load_indexed_frames(frame_path)
        frame_index = expected["rewrite_final_frame"]
        if len(frames) != expected["rewrite_video"]["frames"] or \
                frame_index != len(frames) - 1:
            raise ValueError("FIG summon-replacement final frame index differs")
        pixels, palette = frames[frame_index]
        rgb = expand_rgb(pixels, palette)
        if sha256(pixels) != expected["rewrite_indexed_sha256"] or \
                sha256(palette) != expected["rewrite_palette_sha256"] or \
                sha256(rgb) != expected["rewrite_rgb_sha256"] or \
                sha256(rgb) != expected["original_rgb_sha256"] or \
                sha256(rgb) != expected["capture_rgb_sha256"]:
            raise ValueError("FIG summon-replacement page differs from original")

        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256", "capture_review_png_sha256"):
            digest(expected.get(name), name)
        if expected.get("capture_review_frame") != 6339:
            raise ValueError("FIG summon-replacement review frame differs")
        print(
            "FIG summon-replacement checkpoint: original RGB exact, "
            "inventory packing and resolution pose locked"
        )
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG summon-replacement checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
