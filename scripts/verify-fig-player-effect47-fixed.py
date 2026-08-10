#!/usr/bin/env python3
"""Replay fixed-battle ability 7 and lock effect 47h's rejection unwind."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "initial_command_page",
    "ability_command_selected",
    "escape_ability_page",
    "common_pose0",
    "common_pose4",
    "monster_action_card_without_failure_card",
    "monster_shake_step1",
    "monster_shake_step2",
    "monster_shake_step3",
    "monster_shake_step4",
    "monster_shake_step5",
    "monster_shake_step6",
    "monster_shake_step7",
    "monster_shake_step8",
    "pre_damage_clean",
    "monster_damage_rise1",
    "monster_damage_rise2",
    "monster_damage_rise3",
    "monster_damage_rise4",
    "monster_damage_rise5",
    "monster_damage_rise6",
    "monster_damage_rise7",
    "monster_damage_rise8",
    "monster_damage_rise9",
    "monster_damage_rise10",
    "bare_round_boundary",
    "next_command_page",
)

EXPECTED_REWRITE_FRAMES = (
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed fixed FIG effect-47 digest {label}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = reference.get("matched_frames")
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != "original_fig_player_effect47_fixed" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("formation_directory_offset") != 392 or \
                reference.get("ability_id") != 7 or \
                reference.get("effect_code") != 0x47 or \
                reference.get("resource_cost") != 150 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES:
            raise ValueError("unsupported fixed FIG player-effect-47 reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from fixed player-effect-47 reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"] or \
                sha256((args.game / "ORC.EXE").read_bytes()) != \
                reference["fixture_orc_sha256"]:
            raise ValueError("fixed FIG player-effect-47 staged game differs")
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("fixed FIG player-effect-47 input evidence differs")
        if reference.get("capture_wait_seconds") != 5 or \
                reference.get("capture_pace_seconds") != 1 or \
                reference.get("capture_time_limit_seconds") != 13 or \
                reference.get("capture_review_fps") != 70 or \
                reference.get("capture_review_frames") != 909 or \
                reference.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("fixed FIG player-effect-47 capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)

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
        if trace.get("input") != reference["rewrite_input"] or \
                trace.get("boundaries") != reference["rewrite_boundaries"]:
            raise ValueError("fixed FIG player-effect-47 replay boundaries differ")
        if trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    reference["rewrite_final_fnv1a64"]]:
            raise ValueError("fixed FIG player-effect-47 timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("fixed FIG player-effect-47 final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("fixed FIG player-effect-47 did not stay in battle")

        timeline = trace.get("timeline", [])
        frame_times = tuple(
            item["at_milliseconds"] for item in timeline
            if item.get("kind") == "frame")
        if frame_times != tuple(reference["rewrite_frame_times_milliseconds"]):
            raise ValueError("fixed FIG player-effect-47 frame timing differs")
        voices = [item for item in timeline if item.get("kind") == "voice"]
        expected_voices = (
            (0, reference["effect_voice_at_milliseconds"],
             reference["effect_voice_payload_fnv1a64"]),
            (1, reference["monster_voice_at_milliseconds"],
             reference["monster_voice_payload_fnv1a64"]),
        )
        actual_voices = tuple((
            item.get("call"), item.get("at_milliseconds"),
            item.get("payload_fnv1a64")) for item in voices)
        if actual_voices != expected_voices:
            raise ValueError("fixed FIG player-effect-47 voice timeline differs")
        if any(item[2] == "94bb5914d0c4e7cf" for item in actual_voices):
            raise ValueError("fixed FIG player-effect-47 incorrectly used SV3")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("fixed FIG player-effect-47 frame count differs")
        previous_original = -1
        for page in pages:
            if page["original_review_frame"] <= previous_original:
                raise ValueError("fixed FIG player-effect-47 original page order differs")
            previous_original = page["original_review_frame"]
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "fixed FIG player-effect-47 page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        print(
            "Fixed FIG player effect 47 checkpoint: ability page, two poses, "
            "SP071/five-tick unwind, immediate monster turn and next command "
            "match 27 original RGB pages without resource debit, SV3 or a "
            "normal escape-failure card"
        )
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Fixed FIG player effect 47 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
