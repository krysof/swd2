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
            ("input", "total"): 6,
            ("input", "consumed"): 6,
            ("input", "remaining"): 0,
            ("boundaries", "wait"): 4,
            ("boundaries", "poll"): 2,
            ("boundaries", "text"): 0,
            ("video", "frames"): 209,
            ("video", "direct_updates"): 37,
            ("audio", "music_calls"): 4,
            ("audio", "stop_audio_calls"): 3,
        }
        for (section, key), expected_value in expected_values.items():
            actual = data.get(section, {}).get(key)
            if actual != expected_value:
                raise ValueError(
                    f"startup {section}.{key} is {actual!r}, "
                    f"expected {expected_value}")
        if data.get("delay_milliseconds") != 4648:
            raise ValueError("startup cumulative 70-Hz timing differs")
        if data.get("state_fnv1a64") != "1693cf52a3bbdad7":
            raise ValueError("startup final DAQ/shared-state digest differs")
        if data.get("mapz_fnv1a64") != "827f0f1b725a0958":
            raise ValueError("startup final MAPZ.DAQ digest differs")
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
