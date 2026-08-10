#!/usr/bin/env python3
"""Replay and lock type-10 item 237 / selector 47h's stack-unwinding escape path."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "initial_command_page",
    "item_command_selected",
    "effect47_item_page",
    "party_target_page",
    "item_pose0_retained_until_exit",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed FIG effect-47 digest {label}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = reference.get("pages")
        if reference.get("schema_version") != 1 or \
                reference.get("kind") != "original_fig_item_effect47_escape" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("random_directory_base") != 100 or \
                reference.get("redirected_formation_directory_offset") != 288 or \
                reference.get("monster_definition_id") != 368 or \
                reference.get("item_id") != 237 or \
                reference.get("canonical_ability_id") != 97 or \
                reference.get("effect_code") != 0x47 or \
                reference.get("resource_cost") != 35 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    (1, 2, 3, 4, 5):
            raise ValueError("unsupported FIG item-effect-47 reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from item-effect-47 reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"] or \
                sha256((args.game / "ORC.EXE").read_bytes()) != \
                reference["fixture_orc_sha256"]:
            raise ValueError("FIG item-effect-47 staged game differs")
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("FIG item-effect-47 input evidence differs")
        if reference.get("capture_wait_seconds") != 5 or \
                reference.get("capture_pace_seconds") != 1 or \
                reference.get("capture_time_limit_seconds") != 13 or \
                reference.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG item-effect-47 capture boundary differs")
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
            raise ValueError("FIG item-effect-47 replay boundaries differ")
        if trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    reference["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG item-effect-47 timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG item-effect-47 final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "OC",
                "launched": True}]:
            raise ValueError("FIG item-effect-47 did not return OC")

        timeline = trace.get("timeline", [])
        frame_times = {
            item["frame"]: item["at_milliseconds"]
            for item in timeline if item.get("kind") == "frame"
        }
        if tuple(frame_times.get(index) for index in range(1, 6)) != \
                (0, 0, 0, 0, 0):
            raise ValueError("FIG item-effect-47 pose timing differs")
        voices = [item for item in timeline if item.get("kind") == "voice"]
        if len(voices) != 1 or (
                voices[0].get("call"), voices[0].get("at_milliseconds"),
                voices[0].get("payload_fnv1a64")) != (
                    0, reference["effect_voice_at_milliseconds"],
                    reference["effect_voice_payload_fnv1a64"]):
            raise ValueError("FIG item-effect-47 SP071 checkpoint differs")
        # Normal command escape uses SV3 (94bb...), while selector 47h must
        # submit SP071 and then hold the pose for five ticks without a page.
        if voices[0]["payload_fnv1a64"] == "94bb5914d0c4e7cf":
            raise ValueError("FIG item-effect-47 incorrectly used SV3")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("FIG item-effect-47 frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG item-effect-47 page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        print(
            "FIG item effect 47 checkpoint: item/target pages, pose0, SP071, "
            "five-tick unwind and immediate OC return match original without "
            "resource debit, item consumption, SV3 or an extra page"
        )
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG item effect 47 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
