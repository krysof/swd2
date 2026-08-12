#!/usr/bin/env python3
"""Build or verify the original-capture FIG 18-tick wall-clock checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from pathlib import Path


PIT_HZ = 1_193_182
PIT_CLOCKS = 65_536
HOLD_TICKS = 18
START_KINDS = ("attack_buff_expired", "attack_buff_expired_without_medium")
END_KIND = "expiry_tail_clean"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed digest {label}")
    return value


def build(root: Path) -> dict[str, object]:
    program_path = root / "game" / "FIG.EXE"
    program_sha256 = sha256(program_path)
    samples = []
    for path in sorted((root / "scripts").glob("fig-*-rgb-reference.json")):
        reference = json.loads(path.read_text(encoding="utf-8"))
        fps = reference.get("capture_review_fps")
        frames = reference.get("matched_frames")
        hold = reference.get("expiry_hold_milliseconds")
        if fps != 70 or \
                not isinstance(frames, list) or hold not in (988, 989):
            continue
        recorded_program_sha256 = digest(
            reference.get("reference_program_sha256"),
            path.name + "/reference_program_sha256")
        if recorded_program_sha256 != program_sha256:
            raise ValueError(f"FIG.EXE digest mismatch: {path.name}")
        start = next((frame for frame in frames
                      if frame.get("kind") in START_KINDS), None)
        if start is None:
            continue
        end = next((frame for frame in frames
                    if frame.get("kind") == END_KIND and
                    frame.get("rewrite_frame", -1) >
                    start.get("rewrite_frame", -1)), None)
        if end is None:
            continue
        start_frame = start.get("original_review_frame")
        end_frame = end.get("original_review_frame")
        if not isinstance(start_frame, int) or not isinstance(end_frame, int) or \
                end_frame <= start_frame:
            raise ValueError(f"invalid original timing pair: {path.name}")
        samples.append({
            "reference": str(path.relative_to(root)),
            "reference_sha256": sha256(path),
            "reference_program_sha256": recorded_program_sha256,
            "capture_video_sha256": digest(
                reference.get("capture_video_sha256"),
                path.name + "/capture_video_sha256"),
            "capture_manifest_sha256": digest(
                reference.get("capture_manifest_sha256"),
                path.name + "/capture_manifest_sha256"),
            "review_fps": fps,
            "start_kind": start["kind"],
            "start_frame": start_frame,
            "end_kind": end["kind"],
            "end_frame": end_frame,
            "observed_milliseconds": round(
                (end_frame - start_frame) * 1000.0 / fps, 6),
            "rewrite_hold_milliseconds": hold,
        })
    if len(samples) != 36:
        raise ValueError(f"expected 36 original timing pairs, found {len(samples)}")
    expected = HOLD_TICKS * PIT_CLOCKS * 1000.0 / PIT_HZ
    one_tick = PIT_CLOCKS * 1000.0 / PIT_HZ
    values = [sample["observed_milliseconds"] for sample in samples]
    median = statistics.median(values)
    within_one_tick = sum(abs(value - expected) <= one_tick for value in values)
    if abs(median - expected) > 1000.0 / 70.0 or within_one_tick < 28:
        raise ValueError("original capture does not corroborate the PIT period")
    return {
        "schema_version": 1,
        "kind": "original_fig_timer_wall_clock_checkpoint",
        "status": "in_progress",
        "reference_program": str(program_path.relative_to(root)),
        "reference_program_sha256": program_sha256,
        "pit_input_hz": PIT_HZ,
        "pit_clocks_per_tick": PIT_CLOCKS,
        "hold_ticks": HOLD_TICKS,
        "expected_hold_milliseconds": round(expected, 6),
        "sample_count": len(samples),
        "median_observed_milliseconds": round(median, 6),
        "median_error_milliseconds": round(abs(median - expected), 6),
        "samples_within_one_tick": within_one_tick,
        "limitation": (
            "The 70-fps RGB review matches select stable observations inside "
            "each held page, not instrumented transition edges; the robust "
            "median corroborates the period but does not replace a complete "
            "original/rewrite wall-clock playthrough."),
        "samples": samples,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    try:
        actual = build(root)
        if args.verify:
            expected = json.loads(args.checkpoint.read_text(encoding="utf-8"))
            if actual != expected:
                raise ValueError("registered wall-clock checkpoint differs")
        else:
            args.checkpoint.parent.mkdir(parents=True, exist_ok=True)
            args.checkpoint.write_text(
                json.dumps(actual, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
        print(
            "FIG original wall clock: "
            f"{actual['sample_count']} captures, median "
            f"{actual['median_observed_milliseconds']:.3f}ms vs "
            f"{actual['expected_hold_milliseconds']:.3f}ms PIT hold, "
            f"{actual['samples_within_one_tick']} within one tick")
        return 0
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"FIG original wall clock: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
