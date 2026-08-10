#!/usr/bin/env python3
"""Replay and lock FIG 23b1/2731 mediator installation and dismissal."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_FRAMES = (5, 36, 37, 38, 47)
PAGE_LABELS = (
    "23b1 fixed mediator-install name page",
    "20e7 bare preparation page with installed mediator",
    "2731 dismissal name page after seven ticks",
    "2731 retained alternate Mode-X page",
    "22e0 bare cleanup page after mediator removal",
)
FLIP_FRAMES = tuple(range(38, 47))
FLIP_TIMES = (720, 720, 734, 748, 762, 776, 790, 804, 818)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed FIG medium-dismiss digest {label}")


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
                    "original_fig_medium_summon_dismiss_sequence" or \
                reference.get("status") != "exact_rgb_checkpoint" or \
                reference.get("formation_directory_offset") != 956 or \
                (reference.get("summoner_definition_id"),
                 reference.get("dismisser_definition_id")) != (402, 502) or \
                (reference.get("request_ability_id"),
                 reference.get("fixed_summon_name_ability_id"),
                 reference.get("dismiss_ability_id")) != (54, 51, 53) or \
                reference.get("medium_index") != 0 or \
                not isinstance(pages, list) or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_FRAMES or \
                tuple(page.get("label") for page in pages) != PAGE_LABELS:
            raise ValueError("unsupported FIG medium-dismiss reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                reference["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from medium-dismiss reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                reference["fixture_save_sha256"]:
            raise ValueError("FIG medium-dismiss staged SAVE differs")
        if sha256((args.game / "ITEM.EXE").read_bytes()) != \
                reference["fixture_item_sha256"]:
            raise ValueError("FIG medium-dismiss staged ITEM differs")
        autotype = args.reference.with_name(reference["capture_autotype"])
        replay = args.reference.with_name(reference["replay_input"])
        if sha256(autotype.read_bytes()) != \
                reference["capture_autotype_sha256"]:
            raise ValueError("FIG medium-dismiss original AUTOTYPE differs")
        if sha256(replay.read_bytes()) != reference["replay_input_sha256"]:
            raise ValueError("FIG medium-dismiss replay input differs")
        if reference.get("capture_wait_seconds") != 5 or \
                reference.get("capture_pace_seconds") != 1 or \
                reference.get("capture_time_limit_seconds") != 15 or \
                reference.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG medium-dismiss capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(reference.get(name), name)

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
        if trace.get("input") != {
                "total": 4, "consumed": 4, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 4, "poll": 0, "text": 0, "frontend": 313}:
            raise ValueError("FIG medium-dismiss replay boundaries differ")
        if trace.get("video") != reference["rewrite_video"] or \
                trace.get("audio") != reference["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    reference["rewrite_delay_milliseconds"]:
            raise ValueError("FIG medium-dismiss timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    reference["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG medium-dismiss final save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG medium-dismiss replay missed IF entry")

        frame_times = {
            item["frame"]: item["at_milliseconds"]
            for item in trace.get("timeline", []) if item.get("kind") == "frame"
        }
        if tuple(frame_times.get(index) for index in FLIP_FRAMES) != \
                FLIP_TIMES or frame_times.get(36) != 620 or \
                frame_times.get(37) != 620 or frame_times.get(47) != 903:
            raise ValueError("2731 eight-flip or 22e0 timing differs")
        voices = [
            item for item in trace.get("timeline", [])
            if item.get("kind") == "voice"
        ]
        call = reference["dismiss_voice_call"]
        if call >= len(voices) or voices[call] != {
                "sequence": voices[call]["sequence"],
                "at_milliseconds": reference["dismiss_voice_at_milliseconds"],
                "kind": "voice", "call": call,
                "payload_fnv1a64":
                    reference["dismiss_voice_payload_fnv1a64"]}:
            raise ValueError("2731 SP061 voice submission differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != reference["rewrite_video"]["frames"]:
            raise ValueError("FIG medium-dismiss frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG medium-dismiss page differs from original: " +
                    page["label"])
            digest(page.get("original_png_sha256"),
                   page["label"] + "/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int) or \
                    page["original_review_frame"] <= 0:
                raise ValueError(
                    "invalid original review frame: " + page["label"])

        stencil = reference["stencil_checkpoint"]
        if stencil.get("status") != "original_instruction_checkpoint" or \
                (stencil.get("disassembly_entry"),
                 stencil.get("sprite_dispatch"),
                 stencil.get("mode0_renderer")) != ("2731", "65f1", "684f"):
            raise ValueError("FIG medium-dismiss stencil evidence differs")
        stencil_pixels, stencil_palette = frames[stencil["rewrite_frame"]]
        stencil_rgb = expand_rgb(stencil_pixels, stencil_palette)
        if sha256(stencil_pixels) != stencil["rewrite_indexed_sha256"] or \
                sha256(stencil_palette) != \
                    stencil["rewrite_palette_sha256"] or \
                sha256(stencil_rgb) != stencil["rewrite_rgb_sha256"]:
            raise ValueError("2731/65f1/684f Mode-X stencil page differs")
        alternate = frames[38]
        stencil_page = frames[39]
        for index in (40, 42, 44, 46):
            if frames[index] != alternate:
                raise ValueError("2731 retained Mode-X page alternation differs")
        for index in (41, 43, 45):
            if frames[index] != stencil_page:
                raise ValueError("2731 mode-0 stencil alternation differs")

        print(
            "FIG medium-dismiss checkpoint: 23b1 fixed name, 2731 SP061, "
            "mode-0 stencil, eight flips and bare 22e0 cleanup match")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG medium-dismiss checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
