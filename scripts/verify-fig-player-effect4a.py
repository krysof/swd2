#!/usr/bin/env python3
"""Replay reachable ability 11 and lock effect 4ah's complete stable RGB sequence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "common_pose0", "common_pose4",
    "darkening_step2", "darkening_step3", "darkening_step4", "darkening_step5",
    "sp074_cycle1_frame0", "sp074_cycle1_frame1", "sp074_cycle1_frame2",
    "sp074_cycle1_frame3", "sp074_cycle2_frame0", "sp074_cycle2_frame1",
    "sp074_cycle2_frame2", "sp074_cycle2_frame3", "damage_flash",
    "damage_rise1", "damage_rise2", "damage_rise3", "damage_rise4",
    "damage_rise5", "damage_rise6", "damage_rise7", "damage_rise8",
    "damage_rise9", "damage_rise10", "restoration_step3", "restoration_step4",
    "restoration_step5", "restored_action_card", "player_action_boundary",
    "monster_shake_shifted", "monster_shake_clean", "monster_damage_rise1",
    "monster_damage_rise9", "monster_damage_result", "monster_turn_cleanup",
    "bare_round_boundary", "next_command",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed player-effect-4a evidence digest {label}")
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
                expected.get("kind") != "original_fig_player_effect" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("ability_id") != 11 or \
                expected.get("effect_code") != 0x4A:
            raise ValueError("unsupported FIG player-effect-4a reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the player-effect-4a reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original player-effect-4a input differs")
        if sha256((args.save_dir / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG player-effect-4a staged save differs")

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
            raise ValueError("FIG player-effect-4a input differs")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG player-effect-4a video differs")
        if trace.get("delay_milliseconds") != \
                expected["rewrite_delay_milliseconds"] or \
                trace.get("audio") != expected["rewrite_audio"]:
            raise ValueError("FIG player-effect-4a timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG player-effect-4a final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG player-effect-4a did not use IF entry")

        frames = load_indexed_frames(frame_path)
        pages = expected.get("matched_frames")
        if len(frames) != expected["rewrite_video"]["frames"] or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS:
            raise ValueError("FIG player-effect-4a page set differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG player-effect-4a {page['kind']} no longer matches original")
            digest(page.get("original_png_sha256"),
                   f"{page['kind']}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("FIG player-effect-4a review frame differs")
        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or "DAC" not in limitation:
            raise ValueError("FIG player-effect-4a capture limitation missing")
        print(
            "FIG player effect 4a checkpoint: 38 stable pose/SP074/damage/"
            "restoration/tail pages exactly match original RGB"
        )
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG player effect 4a checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
