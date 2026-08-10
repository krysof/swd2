#!/usr/bin/env python3
"""Replay and lock item 193's empty-slot medium-dismissal return path."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_PAGES = {
    "original_fig_player_empty_medium_dismissal": (
        "item_pose0", "first_observed_dark_step", "darkest_page",
        "restored_pose0", "player_action_boundary", "monster_action_card",
        "attack_shock_page", "rising_damage_midpoint",
        "rising_damage_final", "next_command_paid",
    ),
    "original_fig_learned_empty_medium_dismissal": (
        "common_pose0", "common_pose4", "first_observed_dark_step",
        "darkest_page", "restored_pose4", "player_action_boundary",
        "monster_action_card", "attack_shock_page", "rising_damage_final",
        "next_command_paid",
    ),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed empty-medium digest {label}")
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
        kind = expected.get("kind")
        direct_item = kind == "original_fig_player_empty_medium_dismissal"
        learned = kind == "original_fig_learned_empty_medium_dismissal"
        identity_ok = (
            direct_item and (
                expected.get("item_id"),
                expected.get("canonical_ability_id"),
                expected.get("effect_code"), expected.get("resource_cost"),
                expected.get("medium_slot"), expected.get("empty_x_sentinel"),
                expected.get("payment_pool"),
            ) == (193, 53, 0x3D, 20, 0, 0x50, "ability_points")
        ) or (
            learned and (
                expected.get("ability_id"), expected.get("effect_code"),
                expected.get("resource_cost"), expected.get("resource_class"),
                expected.get("medium_slot"), expected.get("empty_x_sentinel"),
            ) == (53, 0x3D, 20, 4, 0, 0x50)
        )
        if expected.get("schema_version") != 1 or not identity_ok or \
                expected.get("formation_directory_offset") != 392 or \
                (expected.get("capture_wait_seconds"),
                 expected.get("capture_pace_seconds"),
                 expected.get("capture_time_limit_seconds"),
                 expected.get("capture_review_fps"),
                 expected.get("capture_review_frames")) != \
                (5, 1, 15, 70, 1051):
            raise ValueError("unsupported FIG empty-medium reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"] or \
                (direct_item and
                 sha256((args.game / "ITEM.EXE").read_bytes()) !=
                 expected["item_archive_sha256"]):
            raise ValueError("FIG/ITEM data differs from empty-medium reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_sha256"]:
            raise ValueError("FIG empty-medium inputs differ")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG empty-medium staged save differs")

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
            raise ValueError("FIG empty-medium timeline differs")
        if trace.get("frame_fnv1a64", [])[-1:] != [
                expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG empty-medium final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG empty-medium did not use IF entry")

        voices = [entry for entry in trace.get("timeline", [])
                  if entry.get("kind") == "voice"]
        checks = expected.get("voice_checkpoints")
        if not isinstance(checks, list) or len(voices) != len(checks):
            raise ValueError("FIG empty-medium voice count differs")
        for voice, check in zip(voices, checks):
            if (voice.get("call"), voice.get("at_milliseconds"),
                    voice.get("payload_fnv1a64")) != (
                        check.get("call"), check.get("at_milliseconds"),
                        check.get("payload_fnv1a64")):
                raise ValueError("FIG empty-medium voice timing differs")

        frames = load_indexed_frames(frame_path)
        pages = expected.get("matched_frames")
        if len(frames) != expected["rewrite_video"]["frames"] or \
                not isinstance(pages, list) or tuple(
                    page.get("kind") for page in pages) != EXPECTED_PAGES[kind]:
            raise ValueError("FIG empty-medium page set differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG empty-medium {page['kind']} differs from original")
            digest(page.get("original_png_sha256"), "original_png_sha256")
        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        source = "item 193" if direct_item else "learned ability 53"
        print(
            f"FIG empty-medium checkpoint: {source} pays 20 AP, skips the "
            "eight flips at x=50h, and matches ten original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG empty-medium checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
