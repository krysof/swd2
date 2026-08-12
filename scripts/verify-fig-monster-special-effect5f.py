#!/usr/bin/env python3
"""Lock shipped monster special ability 139 / effect 5F."""

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
        raise ValueError(f"malformed special-effect-5F digest {label}")
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
        excluded = [0, 4, 13, 17, 36, 40]
        wanted = [index for index in range(58) if index not in excluded]
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != \
                    "original_fig_monster_special_effect5f_sequence" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                tuple(reference.get(name) for name in (
                    "formation_directory_offset", "monster_definition_id",
                    "ability_id", "effect_code", "target_flags",
                    "status_duration")) != (46, 514, 139, 0x5F, 0xA103, 2) or \
                reference.get("excluded_rewrite_frames") != excluded or \
                "No equality is claimed" not in \
                    reference.get("excluded_reason", "") or \
                not isinstance(pages, list) or len(pages) != 52 or \
                [page.get("rewrite_frame") for page in pages] != wanted:
            raise ValueError("unsupported FIG special-effect-5F reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"] or \
                sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("special-effect-5F program or fixture differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("special-effect-5F input evidence differs")
        if (reference.get("capture_harness_sha256"),
                reference.get("capture_wait_seconds"),
                reference.get("capture_pace_seconds"),
                reference.get("capture_time_limit_seconds")) != (
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba",
                    5, 1, 30):
            raise ValueError("special-effect-5F capture boundary differs")

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
            raise ValueError("special-effect-5F deterministic trace differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("special-effect-5F replay missed IF entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 58:
            raise ValueError("special-effect-5F frame boundary differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "special-effect-5F page differs from original: " +
                    str(page["rewrite_frame"]))
            digest(page.get("original_png_sha256"),
                   f"frame {page['rewrite_frame']}/original_png_sha256")
        print(
            "FIG special effect-5F checkpoint: ability 139 status, two "
            "autonomous attack rounds and recovery match 52 original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG special-effect-5F checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
