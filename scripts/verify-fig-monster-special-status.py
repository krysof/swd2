#!/usr/bin/env python3
"""Replay and lock FIG 26af's successful selected-player status sequence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_FRAMES = (3, 4, 5, 6, 7, 8, 9, 30, 31, 33)
PAGE_LABELS = (
    "target selection immediately before the status action",
    "20e7 bare preparation page",
    "26f3 selected-party card preparation",
    "262f ability-name card over the party card",
    "status icon committed while retaining the name card",
    "22e0 bare cleanup page",
    "following monster physical-attack card",
    "0694 skipped-player recovery card retains own-slot pose zero",
    "0d98 bare cleanup after skipped-player recovery",
    "post-recovery replay exit page",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed monster-special-status digest {label}")


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
                    "original_fig_monster_special_status_sequence" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("formation_directory_offset") != 962 or \
                reference.get("monster_definition_id") != 456 or \
                (reference.get("ability_id"), reference.get("effect_code")) != \
                    (117, 0x64) or \
                "0da7" not in reference.get("rewrite_timeline_note", "") or \
                "pose zero" not in reference.get("rewrite_timeline_note", "") or \
                not isinstance(pages, list) or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_FRAMES or \
                tuple(page.get("label") for page in pages) != PAGE_LABELS:
            raise ValueError("unsupported FIG monster-special-status reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from special-status reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("monster-special-status staged SAVE differs")
        autotype = args.reference.with_name(reference["capture_autotype"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"]:
            raise ValueError("monster-special-status original AUTOTYPE differs")
        if reference.get("capture_wait_seconds") != 5 or \
                reference.get("capture_pace_seconds") != 1 or \
                reference.get("capture_time_limit_seconds") != 30 or \
                reference.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("monster-special-status capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)
        for name in ("tail_capture_video_sha256",
                     "tail_capture_manifest_sha256"):
            digest(reference.get(name), name)
        if reference.get("tail_capture_review_fps") != 70 or \
                reference.get("tail_capture_review_frames") != 2099 or \
                reference.get("tail_capture_video_frames") != 2102 or \
                reference.get("tail_capture_dosbox_exit_code") != 0 or \
                "stable tail pages" not in \
                    reference.get("tail_capture_limitation", ""):
            raise ValueError("monster-special-status tail capture differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        replay = args.reference.with_name(
            "replay-fig-monster-special-status.txt")
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != {
                "total": 4, "consumed": 4, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 4, "poll": 0, "text": 0, "frontend": 236}:
            raise ValueError("monster-special-status replay boundaries differ")
        if trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"]:
            raise ValueError("monster-special-status timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("monster-special-status final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("monster-special-status replay missed IF entry")

        frame_times = {
            item["frame"]: item["at_milliseconds"]
            for item in trace.get("timeline", []) if item.get("kind") == "frame"
        }
        if tuple(frame_times.get(index) for index in range(4, 10)) != \
                (0, 0, 0, 384, 659, 824):
            raise ValueError("26af selected-player status flip timing differs")
        if tuple(frame_times.get(index) for index in (29, 30, 31, 32, 33)) != \
                (2307, 2472, 3460, 3735, 3845) or \
                frame_times[31] - frame_times[30] != 988 or \
                frame_times[32] - frame_times[31] != 275:
            raise ValueError("0694/0da7/0d98 recovery timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("monster-special-status frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "monster-special-status page differs from original: "
                    f"{page['label']}")
            digest(page.get("original_png_sha256"),
                   f"{page['label']}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int) or \
                    page["original_review_frame"] <= 0:
                raise ValueError(
                    "invalid original review frame: " + page["label"])

        # With one actor, 137a places pose zero at its own x=48 action anchor;
        # 0db5 subtracts eight Mode-X columns and produces the x=16 four-column
        # panel.  The exact original page above additionally includes the
        # fighter down to y=199, so compare its whole changed footprint with
        # 0d98's following bare page rather than merely checking the text.
        recovery_pixels = frames[30][0]
        clean_pixels = frames[31][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(recovery_pixels, clean_pixels)) if left != right
        ]
        if len(changed) != 3677 or (
                min(x for x, _ in changed), min(y for _, y in changed),
                max(x for x, _ in changed), max(y for _, y in changed)) != \
                (16, 120, 95, 199):
            raise ValueError("skipped-player recovery lost pose-zero anchor")

        print(
            "FIG monster-special-status checkpoint: 20e7/26f3/262f/status/"
            "22e0 plus skipped-player 0da7/0d98 pages and timing exactly "
            "match original RGB")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG monster-special-status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
