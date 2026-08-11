#!/usr/bin/env python3
"""Replay and lock FIG's captured-ally special ability presentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_KINDS = (
    "ally_ability_card",
    *(f"effect_pair_{index}" for index in range(1, 32)),
    "effect_pair_32_unobserved_stable",
    "resistance_card", "ally_clean_1", "ally_clean_2",
    "monster_action_card", "monster_pre_shake_bare",
    "monster_shake_first", "monster_post_shake_bare",
    *(f"monster_result_{index}" for index in range(1, 11)),
    "monster_result_clean",
)
PAGE_INDICES = (
    30, *range(31, 63), *range(63, 70), *range(76, 87),
)
UNOBSERVED = "effect_pair_32_unobserved_stable"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed captured-ally special digest {label}")


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
        pages = expected.get("full_modern_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_captured_ally_special_ability" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("captured_item_id") != 320 or \
                expected.get("captured_ally_ability_id") != 58 or \
                expected.get("captured_ally_ability_slot") != "special_a" or \
                expected.get("effect_code") != 0x65 or \
                expected.get("random_cursor") != 0x1002 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != PAGE_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_INDICES:
            raise ValueError("unsupported FIG captured-ally special reference")

        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from captured-ally special reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG captured-ally special staged save differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG captured-ally special input evidence differs")
        if expected.get("capture_harness_sha256") != \
                "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba" or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 0.5 or \
                expected.get("capture_time_limit_seconds") != 25 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 1749 or \
                expected.get("capture_dosbox_exit_code") != 0:
            raise ValueError("FIG captured-ally special capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)

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
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG captured-ally special timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG captured-ally special save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG captured-ally special missed IF entry")
        frame_times = [
            item.get("at_milliseconds") for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        if [frame_times[index] for index in PAGE_INDICES] != [
                page["rewrite_at_milliseconds"] for page in pages]:
            raise ValueError("FIG captured-ally special frame timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG captured-ally special frame count differs")
        exact_kinds = []
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"]:
                raise ValueError(
                    f"FIG ally special {page['kind']} modern page differs")
            if "original_rgb_sha256" in page:
                exact_kinds.append(page["kind"])
                if sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG ally special {page['kind']} differs from original")
                digest(page.get("original_png_sha256"),
                       f"{page['kind']}/original_png_sha256")
                if not isinstance(page.get("original_review_frame"), int):
                    raise ValueError("captured-ally special review frame differs")
        if tuple(exact_kinds) != tuple(
                kind for kind in PAGE_KINDS if kind != UNOBSERVED) or \
                expected.get("unobserved_stable_original_pages") != [UNOBSERVED]:
            raise ValueError("FIG captured-ally special evidence set differs")

        # 1048 never enters 2bb5 while the packed ally owns the action.  The
        # left edge of the bottom-card slot therefore stays identical to the
        # bare 1039 return even while the paired ST effects legitimately cross
        # much of the rest of the lower screen.
        bare = frames[64][0]
        for index in range(30, 64):
            pixels = frames[index][0]
            if any(
                    pixels[y * 320:y * 320 + 12] !=
                    bare[y * 320:y * 320 + 12]
                    for y in range(150, 200)):
                raise ValueError(
                    f"FIG ally special frame {index} retained a party card")
        if frames[64][0] != frames[65][0]:
            raise ValueError("FIG ally special 1039 bare return changed")

        print(
            "FIG captured-ally special checkpoint: action card, 32 paired "
            "effect pages, resistance return and following monster result "
            "retain the card-free 1048/1039 path; 50 stable RGB pages match "
            "the original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG captured-ally special checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
