#!/usr/bin/env python3
"""Replay and lock direct type-10 support-item pages and deferred AP debit."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_PAGES = (
    "item_pose0", "support_before", "support_after",
    "player_action_boundary", "post_debit_monster_action_card",
    "post_debit_monster_attack_pose",
)
EXPECTED_ITEMS = {
    190: (50, 0x01, 4, 7),
    242: (102, 0x0A, 5, 30),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed item-support digest {label}")
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
        item_id = expected.get("item_id")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_direct_item_support" or \
                expected.get("formation_directory_offset") != 392 or \
                item_id not in EXPECTED_ITEMS or \
                (expected.get("canonical_ability_id"),
                 expected.get("effect_code"), expected.get("resource_class"),
                 expected.get("resource_cost")) != EXPECTED_ITEMS[item_id] or \
                expected.get("payment_pool") != "ability_points" or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 0.5 or \
                expected.get("capture_time_limit_seconds") != 15:
            raise ValueError("unsupported FIG item-support reference")
        if item_id == 242:
            for name in ("zero_ap_capture_video_sha256",
                         "zero_ap_capture_manifest_sha256",
                         "zero_ap_fixture_save_sha256",
                         "zero_ap_notice_png_sha256",
                         "zero_ap_notice_rgb_sha256"):
                digest(expected.get(name), name)
            if expected.get("zero_ap_notice_review_frame") != 450:
                raise ValueError("FIG class-5 item zero-AP review frame differs")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"] or \
                sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["item_archive_sha256"]:
            raise ValueError("FIG/ITEM data differs from item-support reference")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_sha256"]:
            raise ValueError("FIG item-support inputs differ")
        if sha256((args.save_dir / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG item-support staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_dir), "--slot", "1", "--no-save",
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
            raise ValueError("FIG item-support timeline differs")
        if trace.get("frame_fnv1a64", [])[-1:] != [
                expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG item-support final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG item-support did not use IF entry")

        frames = load_indexed_frames(frame_path)
        pages = expected.get("matched_frames")
        if len(frames) != expected["rewrite_video"]["frames"] or \
                not isinstance(pages, list) or tuple(
                    page.get("kind") for page in pages) != EXPECTED_PAGES:
            raise ValueError("FIG item-support page set differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG item-support {page['kind']} differs from original")
            digest(page.get("original_png_sha256"),
                   f"{page.get('kind')}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("FIG item-support review frame differs")

        for name in ("capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)
        print(
            f"FIG direct item-support checkpoint: item {item_id} keeps pose0 "
            "and pre-debit AP through its support selector, then exposes the "
            f"{expected['resource_cost']}-point 585e AP debit before the next "
            "monster action")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item-support checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
