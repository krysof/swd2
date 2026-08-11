#!/usr/bin/env python3
"""Replay and lock captured-ally special-B mediator dismissal."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


PAGE_KINDS = (
    "af_installed_bare",
    "ally_ability_action_card",
    "dismissal_bare_preflip",
    "dismissal_stencil_flip_1",
    "dismissal_bare_flip_1",
    "dismissal_stencil_flip_2",
    "dismissal_bare_flip_2",
    "dismissal_stencil_flip_3",
    "dismissal_bare_flip_3",
    "dismissal_stencil_flip_4_unobserved_stable",
    "dismissal_bare_flip_4",
    "ally_clean_after_af_removal",
)
PAGE_INDICES = tuple(range(70, 82))
UNOBSERVED = "dismissal_stencil_flip_4_unobserved_stable"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed ally-dismiss digest {label}")


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
                    "original_fig_captured_ally_medium_dismissal" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("captured_item_id") != 394 or \
                expected.get("captured_ally_ability_id") != 67 or \
                expected.get("captured_ally_ability_slot") != "special_b" or \
                expected.get("effect_code") != 0x3e or \
                expected.get("installed_medium_ability_id") != 66 or \
                expected.get("medium_index") != 1 or \
                expected.get("random_cursor") != 0x1004 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != PAGE_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    PAGE_INDICES:
            raise ValueError("unsupported FIG captured-ally dismissal reference")

        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from captured-ally dismissal reference")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG captured-ally dismissal staged save differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG captured-ally dismissal input evidence differs")
        if expected.get("capture_harness_sha256") != \
                "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba" or \
                expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 0.5 or \
                expected.get("capture_time_limit_seconds") != 25 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 1749 or \
                expected.get("capture_dosbox_exit_code") != 0:
            raise ValueError("FIG captured-ally dismissal capture boundary differs")
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
            raise ValueError("FIG captured-ally dismissal timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG captured-ally dismissal save triple differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG captured-ally dismissal missed IF entry")
        frame_times = [
            item.get("at_milliseconds") for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        if frame_times != expected["rewrite_frame_times_milliseconds"]:
            raise ValueError("FIG captured-ally dismissal frame timing differs")
        voices = [item for item in trace.get("timeline", [])
                  if item.get("kind") == "voice"]
        call = expected["dismiss_voice_call"]
        if call >= len(voices) or (
                voices[call].get("call"), voices[call].get("at_milliseconds"),
                voices[call].get("payload_fnv1a64")) != (
                    call, expected["dismiss_voice_at_milliseconds"],
                    expected["dismiss_voice_payload_fnv1a64"]):
            raise ValueError("FIG captured-ally dismissal SP061 call differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG captured-ally dismissal frame count differs")
        exact_kinds = []
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"]:
                raise ValueError(
                    f"FIG ally dismissal {page['kind']} modern page differs")
            if "original_rgb_sha256" in page:
                exact_kinds.append(page["kind"])
                if sha256(rgb) != page["original_rgb_sha256"]:
                    raise ValueError(
                        f"FIG ally dismissal {page['kind']} differs from original")
                digest(page.get("original_png_sha256"),
                       f"{page['kind']}/original_png_sha256")
                if not isinstance(page.get("original_review_frame"), int):
                    raise ValueError("captured-ally dismissal review frame differs")
        if tuple(exact_kinds) != tuple(
                kind for kind in PAGE_KINDS if kind != UNOBSERVED) or \
                expected.get("unobserved_stable_original_pages") != [UNOBSERVED]:
            raise ValueError("FIG captured-ally dismissal evidence set differs")

        # The four retained clean pages are the same pre-removal 2db8 page;
        # each visible stencil flip is the same 65f1 output. The original 70
        # Hz capture crossed only the fourth stencil's one-tick boundary.
        if any(frames[index][0] != frames[72][0]
               for index in (74, 76, 78, 80)) or \
                any(frames[index][0] != frames[73][0]
                    for index in (75, 77, 79)):
            raise ValueError("FIG captured-ally dismissal page alternation differs")
        stencil = frames[73][0]
        bare = frames[72][0]
        changed = [
            (x, y) for y in range(1, 49) for x in range(250, 320)
            if stencil[y * 320 + x] != bare[y * 320 + x]
        ]
        if len(changed) != 924 or \
                min(x for x, _ in changed) != 273 or \
                max(x for x, _ in changed) != 293 or \
                any(stencil[y * 320 + x] != 0x0f for x, y in changed):
            raise ValueError("FIG captured-ally AF mode-0 stencil differs")
        clean = frames[81][0]
        for index in PAGE_INDICES:
            pixels = frames[index][0]
            if any(pixels[y * 320:y * 320 + 12] !=
                   clean[y * 320:y * 320 + 12]
                   for y in range(150, 200)):
                raise ValueError("FIG captured-ally dismissal retained a party card")

        print(
            "FIG captured-ally medium-dismiss checkpoint: special B 67 removes "
            "AF through the card-free 1048 action, SP061 and eight 65f1 flips; "
            "11 stable RGB pages match original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG captured-ally medium-dismiss checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
