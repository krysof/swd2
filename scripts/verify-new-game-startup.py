#!/usr/bin/env python3
"""Lock the deterministic MT -> ED -> DEMO -> OM startup checkpoint."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        data = json.loads(args.trace.read_text(encoding="utf-8"))
        transitions = [
            (item.get("module"), item.get("input"), item.get("output"),
             item.get("launched"))
            for item in data.get("transitions", [])
        ]
        expected = [
            ("MEO.EXE", "--", "MT", True),
            ("RPG.EXE", "MT", "ED", True),
            ("DEMO.EXE", "ED", "--", True),
            ("RPG.EXE", "OM", "--", True),
        ]
        if transitions != expected:
            raise ValueError(
                f"startup transitions differ: {transitions!r}")
        expected_values = {
            ("input", "total"): 55,
            ("input", "consumed"): 55,
            ("input", "remaining"): 0,
            ("boundaries", "wait"): 5,
            ("boundaries", "poll"): 50,
            ("boundaries", "text"): 0,
            ("video", "frames"): 848,
            ("video", "direct_updates"): 475,
            ("audio", "music_calls"): 7,
            ("audio", "stop_audio_calls"): 4,
        }
        for (section, key), expected_value in expected_values.items():
            actual = data.get(section, {}).get(key)
            if actual != expected_value:
                raise ValueError(
                    f"startup {section}.{key} is {actual!r}, "
                    f"expected {expected_value}")
        if data.get("delay_milliseconds") != 24297:
            raise ValueError("startup cumulative 70-Hz timing differs")
        if data.get("state_fnv1a64") != "ba78081dbd6f56d6":
            raise ValueError("startup final DAQ/shared-state digest differs")
        if data.get("mapz_fnv1a64") != "827f0f1b725a0958":
            raise ValueError("startup final MAPZ.DAQ digest differs")
        if data.get("name_fnv1a64") != "e3d2853e2676513b":
            raise ValueError("startup final NAME.DSK digest differs")
        frame_hashes = data.get("frame_fnv1a64", [])
        if len(frame_hashes) <= 109 or \
                frame_hashes[109] != "86d24d180442b751":
            raise ValueError("startup RPG name-editor frame differs")
        if len(frame_hashes) <= 251 or \
                frame_hashes[251] != "82bf74a2a3cee838":
            raise ValueError(
                "startup DE001 dialogue page is not the opaque original scene")
        checkpoints = data.get("input_checkpoints", [])
        expected_opening_tail = [
            (22, "POLL", "CONFIRM", "707218e265166949"),
            (23, "POLL", "CONFIRM", "707218e265166949"),
            (24, "POLL", "NONE", "22dcae89b63c96d6"),
        ]
        actual_opening_tail = [
            (item.get("index"), item.get("boundary"), item.get("action"),
             item.get("state_fnv1a64"))
            for item in checkpoints[22:25]
        ]
        if actual_opening_tail != expected_opening_tail:
            raise ValueError("startup event-to-world input boundary differs")
        expected_tail = [
            (52, "POLL", "UP", "e5bc2e224edc4804"),
            (53, "POLL", "NONE", "ba78081dbd6f56d6"),
            (54, "POLL", "QUIT", "ba78081dbd6f56d6"),
        ]
        actual_tail = [
            (item.get("index"), item.get("boundary"), item.get("action"),
             item.get("state_fnv1a64"))
            for item in checkpoints[-3:]
        ]
        if actual_tail != expected_tail:
            raise ValueError("startup first MAP0 transition boundary differs")
        if len(frame_hashes) != 848 or \
                frame_hashes[-1] != "ddfa1aa4fc622e55":
            raise ValueError("startup first destination idle world frame differs")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("startup did not stop from the explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"new-game startup validation: FAIL: {error}", file=sys.stderr)
        return 1
    print("new-game startup validation: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
