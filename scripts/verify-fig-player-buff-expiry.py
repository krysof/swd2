#!/usr/bin/env python3
"""Replay and lock FIG's player attack-buff natural-expiry presentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


EXPECTED_KINDS = (
    "retained_action_pose", "attack_buff_expired", "action_tail_clean",
    "following_monster_action_card", "monster_damage_first",
    "monster_damage_final", "next_command",
)
EXPECTED_REWRITE_FRAMES = (310, 311, 312, 313, 323, 330, 335)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed player-buff-expiry digest {label}")
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
                expected.get("kind") != "original_fig_player_buff_expiry" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("random_cursor") != 20 or \
                expected.get("ability_id") != 38 or \
                expected.get("effect_code") != 0x43 or \
                expected.get("expiry_player_turn") != 8 or \
                not isinstance(pages, list) or \
                tuple(page.get("kind") for page in pages) != EXPECTED_KINDS or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    EXPECTED_REWRITE_FRAMES:
            raise ValueError("unsupported FIG player-buff-expiry reference")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the player-expiry reference")
        if sha256((args.game / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"] or \
                sha256((args.game / "MAPZ.DA1").read_bytes()) != \
                expected["fixture_mapz_sha256"] or \
                sha256((args.game / "NAME1.DSK").read_bytes()) != \
                expected["fixture_name_sha256"]:
            raise ValueError("FIG player-buff-expiry fixture differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG player-buff-expiry input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 2 or \
                expected.get("capture_time_limit_seconds") != 60 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 4199 or \
                expected.get("capture_video_frames") != 4204 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != \
                    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba":
            raise ValueError("FIG player-buff-expiry capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)
        limitation = expected.get("capture_limitation")
        if not isinstance(limitation, str) or \
                "3853" not in limitation or "instantaneous" not in limitation or \
                "INT 08h" not in limitation or "18-tick" not in limitation:
            raise ValueError("FIG player-buff-expiry limitation is missing")

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
            raise ValueError("FIG player-buff-expiry replay boundary differs")
        if trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]]:
            raise ValueError("FIG player-buff-expiry timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG player-buff-expiry final state differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--" or \
                trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF", "output": "--",
                    "launched": True}]:
            raise ValueError("FIG player-buff-expiry did not reach replay exit")

        frame_times = [
            item["at_milliseconds"] for item in trace.get("timeline", [])
            if item.get("kind") == "frame"
        ]
        expiry_frame = expected["expiry_rewrite_frame"]
        clean_frame = expected["clean_rewrite_frame"]
        following_frame = expected["following_action_rewrite_frame"]
        if expiry_frame != EXPECTED_REWRITE_FRAMES[1] or \
                clean_frame != expiry_frame + 1 or \
                following_frame != clean_frame + 1 or \
                frame_times[expiry_frame] != expected["expiry_at_milliseconds"] or \
                frame_times[clean_frame] != expected["clean_at_milliseconds"] or \
                frame_times[following_frame] != \
                    expected["following_action_at_milliseconds"] or \
                frame_times[clean_frame] - frame_times[expiry_frame] != \
                    expected["expiry_hold_milliseconds"] or \
                expected["expiry_hold_milliseconds"] != 989:
            raise ValueError("FIG player-buff-expiry event timing differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != expected["rewrite_video"]["frames"]:
            raise ValueError("FIG player-buff-expiry frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "FIG player-buff-expiry page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")

        # FIG 0da7 calls 2db8 then 137a without clearing the just-finished
        # physical action globals.  The expiry card must therefore be the only
        # difference from the immediately retained final pose: four Mode-X
        # columns by 32 scanlines at the attacked monster, not the party slot.
        retained_pixels = frames[expiry_frame - 1][0]
        expiry_pixels = frames[expiry_frame][0]
        changed = [
            (index % 320, index // 320)
            for index, (left, right) in enumerate(
                zip(retained_pixels, expiry_pixels)) if left != right
        ]
        if len(changed) != 2560 or (
                min(x for x, _ in changed), min(y for _, y in changed),
                max(x for x, _ in changed), max(y for _, y in changed)) != \
                (112, 120, 191, 151):
            raise ValueError(
                "FIG player-buff-expiry card lost the retained action anchor")

        # 0d98 follows the 18-tick card with a bare 2db8 page.  Registering
        # frame 312 against the original RGB hash above proves that neither
        # the fighter action nor the ordinary bottom party portrait survives.
        if frames[clean_frame][0] == retained_pixels or \
                frames[clean_frame][0] == expiry_pixels:
            raise ValueError("FIG player-buff-expiry clean page was retained")

        print(
            "FIG player-buff-expiry checkpoint: eighth-turn attack-buff "
            "expiry retains the final physical pose, overlays 0da7 at the "
            "monster anchor, holds 18 ticks, cleans with bare 0d98 and "
            "matches seven original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(
            1, f"FIG player-buff-expiry checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
