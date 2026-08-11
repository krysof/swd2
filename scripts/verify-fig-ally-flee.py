#!/usr/bin/env python3
"""Replay and lock FIG's captured ally random-seven departure tail."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "persistent_ally_before_departure",
    "summoned_ally_flee_card",
    "post_departure_bare_redraw",
    "round_boundary_bare_redraw",
    "next_command_without_ally",
)
EXPECTED_FRAMES = tuple(range(134, 139))
EXPECTED_ORIGINAL_FRAMES = (1170, 1183, 1222, 1246, 1247)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed ally-departure digest {label}")
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
        pages = expected.get("matched_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_captured_ally_departure" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 100 or \
                expected.get("captured_item_id") != 420 or \
                expected.get("random_cursor") != 0x108C or \
                expected.get("departure_random_value") != 7 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_FRAMES or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    EXPECTED_ORIGINAL_FRAMES:
            raise ValueError("unsupported FIG ally-departure reference")

        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from ally-departure reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG ally-departure staged save differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG ally-departure input evidence differs")
        if expected.get("capture_harness_sha256") != \
                "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba" or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 0.25 or \
                expected.get("capture_time_limit_seconds") != 20 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 1401 or \
                expected.get("capture_dosbox_exit_code") != -11:
            raise ValueError("FIG ally-departure capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "finalized all 1,401" not in limitation or \
                "five stable pre-shutdown pages" not in limitation:
            raise ValueError("FIG ally-departure capture limitation missing")

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
            raise ValueError("FIG ally-departure replay timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG ally-departure final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG ally-departure replay missed IF entry")

        voices = [
            {"call": item.get("call"),
             "at_milliseconds": item.get("at_milliseconds"),
             "payload_fnv1a64": item.get("payload_fnv1a64")}
            for item in trace.get("timeline", []) if item.get("kind") == "voice"
        ]
        if voices != expected["rewrite_voice_timeline"] or \
                voices[-1].get("payload_fnv1a64") != \
                    "94bb5914d0c4e7cf":
            raise ValueError("FIG ally-departure SV3 timeline differs")
        frame_times = [
            item.get("at_milliseconds") for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        if frame_times[-5:] != \
                expected["rewrite_final_frame_times_milliseconds"]:
            raise ValueError("FIG ally-departure final frame timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG ally-departure frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG ally-departure page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        if pages[2]["rewrite_rgb_sha256"] != \
                pages[3]["rewrite_rgb_sha256"]:
            raise ValueError("FIG ally-departure bare redraws no longer agree")
        print(
            "FIG ally-departure checkpoint: persistent ally, random-seven flee "
            "card, party-card-free removal redraw, four-tick bare tail and next "
            "command match five stable original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG ally-departure checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
