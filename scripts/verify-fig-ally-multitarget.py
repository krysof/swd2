#!/usr/bin/env python3
"""Replay and lock captured-ally effect 42h across two monster targets."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_KINDS = (
    "ally_ability_card",
    *(f"effect_page_{index}" for index in range(1, 17)),
    "target_1_prepare",
    *(f"target_1_result_{index}" for index in range(1, 11)),
    "target_2_prepare",
    *(f"target_2_result_{index}" for index in range(1, 11)),
    "ally_clean",
)
PAGE_INDICES = tuple(range(38, 78))
UNOBSERVED = {
    "target_1_result_1", "target_2_result_1", "target_2_result_2",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed captured-ally multi-target digest {label}")


def difference_box(before: bytes, after: bytes):
    offsets = [
        offset for offset, values in enumerate(zip(before, after))
        if values[0] != values[1]
    ]
    if not offsets:
        return 0, None
    xs = [offset % 320 for offset in offsets]
    ys = [offset // 320 for offset in offsets]
    return len(offsets), (min(xs), min(ys), max(xs), max(ys))


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
                    "original_fig_captured_ally_multitarget_ability" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 12 or \
                expected.get("monster_definition_ids") != [500, 500] or \
                expected.get("captured_item_id") != 334 or \
                expected.get("captured_ally_ability_id") != 74 or \
                expected.get("effect_code") != 0x42 or \
                expected.get("target_flags") != 0x8401 or \
                expected.get("random_cursor") != 0x1010 or \
                expected.get("damage_per_target") != 38 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != PAGE_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_INDICES:
            raise ValueError("unsupported FIG captured-ally multi-target reference")

        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from multi-target reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG captured-ally multi-target staged save differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG captured-ally multi-target input evidence differs")
        if expected.get("capture_harness_sha256") != \
                "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba" or \
                expected.get("capture_wait_seconds") != 7 or \
                expected.get("capture_pace_seconds") != 0.5 or \
                expected.get("capture_time_limit_seconds") != 30 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 2099 or \
                expected.get("capture_dosbox_exit_code") != 0:
            raise ValueError("FIG captured-ally multi-target capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or "DAC" not in limitation:
            raise ValueError("FIG multi-target capture limitation missing")

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
            raise ValueError("FIG captured-ally multi-target timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG captured-ally multi-target save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG captured-ally multi-target missed IF entry")
        frame_times = [
            item.get("at_milliseconds") for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        if [frame_times[index] for index in PAGE_INDICES] != \
                expected["rewrite_checkpoint_frame_times_milliseconds"]:
            raise ValueError("FIG captured-ally multi-target frame timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG captured-ally multi-target frame count differs")
        exact_kinds = []
        indexed_pages = []
        previous_original = -1
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            indexed_pages.append(pixels)
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"]:
                raise ValueError(
                    f"FIG ally multi-target {page['kind']} modern page differs")
            if "original_rgb_sha256" in page:
                exact_kinds.append(page["kind"])
                if sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG ally multi-target {page['kind']} differs from original")
                digest(page.get("original_png_sha256"),
                       f"{page['kind']}/original_png_sha256")
                original_frame = page.get("original_review_frame")
                if not isinstance(original_frame, int) or \
                        original_frame <= previous_original:
                    raise ValueError("multi-target original page order differs")
                previous_original = original_frame
        if set(PAGE_KINDS) - set(exact_kinds) != UNOBSERVED or \
                set(expected.get("unobserved_stable_original_pages", [])) != \
                    UNOBSERVED:
            raise ValueError("FIG captured-ally multi-target evidence set differs")

        # 5974 runs one effect timeline, then increments 3197 and invokes 59a1
        # once for each living monster. Each 144e call keeps a different
        # reaction scratch; only the 90 indexed digit pixels may change during
        # its ten rising-number pages.
        first_prepare = indexed_pages[17]
        second_prepare = indexed_pages[28]
        clean = indexed_pages[-1]
        if difference_box(clean, first_prepare) != \
                (2652, (80, 69, 140, 143)) or \
                difference_box(clean, second_prepare) != \
                (2652, (180, 69, 240, 143)):
            raise ValueError("FIG multi-target reaction scratch used wrong target")
        for ordinal, (prepare, result_pages, left, right) in enumerate((
                (first_prepare, indexed_pages[18:28], 105, 119),
                (second_prepare, indexed_pages[29:39], 205, 219)), 1):
            for page_index, result in enumerate(result_pages, 1):
                changed = [
                    offset for offset, values in enumerate(zip(prepare, result))
                    if values[0] != values[1]
                ]
                if len(changed) != 90 or any(
                        not (left <= offset % 320 <= right and
                             65 <= offset // 320 <= 107)
                        for offset in changed):
                    raise ValueError(
                        f"FIG target {ordinal} result page {page_index} "
                        "did not retain its own 144e scratch")
        if frames[37] != frames[77]:
            raise ValueError("FIG multi-target action did not use one final 0fb9 tail")

        print(
            "FIG captured-ally multi-target checkpoint: one 16-page effect, "
            "two target-specific 144e scratches, twenty rising-number pages "
            "and one clean tail match 37 stable original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG captured-ally multi-target checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
