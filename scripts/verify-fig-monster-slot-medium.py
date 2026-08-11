#!/usr/bin/env python3
"""Replay and lock FIG's second live monster installing the AF mediator."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_FRAMES = tuple(range(1, 36))
EXPECTED_KINDS = (
    "initial_command_page", "attack_command_selected",
    "three_monster_target_selector", "slot1_medium_prepare_bare",
    "fixed_af_medium_name_card",
    *(f"slot1_medium_flight_step_{index:02d}" for index in range(1, 29)),
    "monster_cleanup_with_installed_af",
    "player_pose0_successor_with_af",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed monster-slot-medium digest {label}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = expected.get("matched_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_fig_second_monster_af_medium_install" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 956 or \
                expected.get("formation_definition_ids") != [402, 502, 385] or \
                expected.get("all_monsters_live") is not True or \
                expected.get("caster_monster_index") != 1 or \
                expected.get("caster_definition_id") != 502 or \
                expected.get("caster_hit_points") != 60000 or \
                expected.get("caster_speed") != 60000 or \
                expected.get("other_monster_speeds") != [0, 0] or \
                expected.get("monster_primary_chance") != 9 or \
                expected.get("monster_primary_ability") != 8 or \
                expected.get("requested_medium_index") != 1 or \
                expected.get("fixed_summon_name_ability_id") != 66 or \
                not isinstance(pages, list) or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_FRAMES or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS:
            raise ValueError("unsupported FIG monster-slot-medium reference")

        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from monster-slot-medium reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "ITEM.EXE").read_bytes()) != \
                expected["fixture_item_sha256"]:
            raise ValueError("monster-slot-medium staged game differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("monster-slot-medium input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 15 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 1050 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("monster-slot-medium capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "35 registered pages" not in limitation or \
                "stable full-frame RGB" not in limitation:
            raise ValueError("monster-slot-medium capture limitation missing")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.game), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"]:
            raise ValueError("monster-slot-medium replay boundaries differ")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("monster-slot-medium audio/video timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("monster-slot-medium final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("monster-slot-medium replay missed IF entry")

        timeline = trace.get("timeline", [])
        frame_times = tuple(item["at_milliseconds"] for item in timeline
                            if item.get("kind") == "frame")
        if frame_times != tuple(expected["rewrite_frame_times_milliseconds"]):
            raise ValueError("monster-slot-medium frame timing differs")
        voices = [
            {"call": item.get("call"),
             "at_milliseconds": item.get("at_milliseconds"),
             "payload_fnv1a64": item.get("payload_fnv1a64")}
            for item in timeline if item.get("kind") == "voice"
        ]
        if voices != expected["rewrite_voice_timeline"]:
            raise ValueError("monster-slot-medium voice timeline differs")
        medium_call = expected.get("medium_voice_call")
        if not isinstance(medium_call, int) or medium_call >= len(voices) or \
                voices[medium_call] != {
                    "call": medium_call,
                    "at_milliseconds": expected["medium_voice_at_milliseconds"],
                    "payload_fnv1a64": expected["medium_voice_payload_fnv1a64"]}:
            raise ValueError("monster-slot-medium SP049 boundary differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("monster-slot-medium frame count differs")
        previous_original = -1
        for page in pages:
            review = page.get("original_review_frame")
            if not isinstance(review, int) or review <= previous_original:
                raise ValueError("monster-slot-medium original order differs")
            previous_original = review
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "monster-slot-medium page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        print(
            "FIG monster-slot-medium checkpoint: live slot-1 prepare, fixed AF "
            "name card, 28 mediator-flight positions, installed-medium cleanup "
            "and player successor match 35 original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG monster-slot-medium checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
