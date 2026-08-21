#!/usr/bin/env python3
"""Verify that copied SW062 gives ITEM 62 the iron-fan physical animation."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


EXPECTED_STATE = "0e5f5d307e334096"
EXPECTED_VIDEO = {
    "frames": 50,
    "direct_updates": 0,
    "last_width": 320,
    "last_height": 200,
    "fnv1a64": "3b277aefb17dba2f",
}
EXPECTED_AUDIO = {
    "music_calls": 1,
    "voice_calls": 2,
    "stop_music_calls": 0,
    "stop_audio_calls": 1,
    "fnv1a64": "ed0abacb5533e132",
}


def run(executable: Path, game: Path, save: Path, replay: Path,
        trace: Path, frames: Path) -> dict[str, object]:
    subprocess.run([
        str(executable), "--game", str(game), "--save-dir", str(save),
        "--slot", "1", "--no-save", "--start-marker", "IF",
        "--run-replay", str(replay), "--trace-output", str(trace),
        "--frame-output", str(frames),
    ], check=True, stdout=subprocess.DEVNULL)
    return json.loads(trace.read_text(encoding="utf-8"))


def normalized_timeline(trace: dict[str, object]) -> list[dict[str, object]]:
    timeline = []
    for raw in trace.get("timeline", []):
        item = dict(raw)
        if item.get("kind") == "input":
            item.pop("state_fnv1a64", None)
        timeline.append(item)
    return timeline


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("replay", type=Path)
    args = parser.parse_args()
    try:
        args.output.mkdir(parents=True, exist_ok=True)
        if (args.game / "SW" / "SW062.RSK").read_bytes() != \
                (args.game / "SW" / "SW124.RSK").read_bytes():
            raise ValueError("SW062 and SW124 differ")

        tianhuo_trace_path = args.output / "tianhuo-trace.json"
        tianhuo_frames = args.output / "tianhuo-frames.bin"
        tianhuo = run(
            args.executable, args.game, args.game, args.replay,
            tianhuo_trace_path, tianhuo_frames)

        iron_save = args.output / "iron-save"
        if iron_save.exists():
            shutil.rmtree(iron_save)
        iron_save.mkdir()
        for name in ("SAVE.DA1", "MAPZ.DA1", "NAME1.DSK"):
            shutil.copy2(args.game / name, iron_save / name)
        iron_state = bytearray((iron_save / "SAVE.DA1").read_bytes())
        iron_state[0x106 + 0x14:0x106 + 0x16] = (124).to_bytes(2, "little")
        (iron_save / "SAVE.DA1").write_bytes(iron_state)

        iron_trace_path = args.output / "iron-trace.json"
        iron_frames = args.output / "iron-frames.bin"
        iron = run(
            args.executable, args.game, iron_save, args.replay,
            iron_trace_path, iron_frames)

        expected_transition = [{
            "module": "FIG.EXE", "input": "IF", "output": "--",
            "launched": True,
        }]
        if tianhuo.get("input") != {
                "total": 3, "consumed": 3, "remaining": 0,
                "implicit_quit_calls": 0} or \
                tianhuo.get("transitions") != expected_transition or \
                tianhuo.get("video") != EXPECTED_VIDEO or \
                tianhuo.get("audio") != EXPECTED_AUDIO or \
                tianhuo.get("delay_milliseconds") != 3296 or \
                tianhuo.get("state_fnv1a64") != EXPECTED_STATE:
            raise ValueError("Tianhuo-fan C++ replay checkpoint differs")

        for field in ("video", "audio", "delay_milliseconds", "frame_fnv1a64"):
            if tianhuo.get(field) != iron.get(field):
                raise ValueError(f"Tianhuo/iron fan {field} differs")
        if normalized_timeline(tianhuo) != normalized_timeline(iron):
            raise ValueError("Tianhuo/iron fan presentation timeline differs")
        if tianhuo_frames.read_bytes() != iron_frames.read_bytes():
            raise ValueError("Tianhuo/iron fan frame stream differs")

        print(
            "FIG Tianhuo-fan checkpoint: ITEM 62 physical attack completes "
            "with zero media and is frame/timeline-identical to copied SW124")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG Tianhuo-fan checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
