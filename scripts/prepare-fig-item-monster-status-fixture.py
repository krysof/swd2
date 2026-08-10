#!/usr/bin/env python3
"""Create direct-item 205 fixtures for applied and resisted monster status."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


CASES = {
    "applied": {
        "formation": 0x04D4,
        "expected": "0f8bbeb39f61e4944756bc5c890e91319f982cbb9353439150e407d941b2ef35",
    },
    "resisted": {
        "formation": 392,
        "expected": "aa83ffdd7f19dc7acb3276f1ac30930adee355e8e60dc19687ecb245a39aa19f",
    },
}


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        original = bytearray((args.game / "SAVE.DA1").read_bytes())
        actor = 0x106
        for name, case in CASES.items():
            output = args.output / name
            output.mkdir()
            for filename in ("MAPZ.DA1", "NAME1.DSK"):
                shutil.copy2(args.game / filename, output / filename)

            data = bytearray(original)
            word(data, 0x10, 1)
            word(data, 0x4A0, case["formation"])
            word(data, actor + 0x08, 0)
            if name == "applied":
                # Monster 338 accepts selector 5eh. Large HP and initiative
                # isolate the item action from the monster's response.
                word(data, actor + 0x0E, 60000)
                word(data, actor + 0x2D, 60000)
                word(data, actor + 0x2F, 60000)
                word(data, actor + 0x5D, 0xFFFF)
            else:
                # Formation 392's monster resists selector 5eh.
                word(data, actor + 0x2D, 1000)
                word(data, actor + 0x2F, 1000)
                word(data, actor + 0x5D, 1000)
            word(data, actor + 0x35, 1000)
            word(data, actor + 0x37, 1000)
            word(data, actor + 0x55, 1000)
            word(data, actor + 0x57, 1000)
            for offset in range(0x382, 0x3E6, 2):
                word(data, offset, 0)
            word(data, 0x382, 205)  # 入夢符, direct selector 5eh

            actual = hashlib.sha256(data).hexdigest()
            if actual != case["expected"]:
                raise ValueError(
                    f"FIG item monster-status {name} save differs: {actual}")
            (output / "SAVE.DA1").write_bytes(data)
            print(f"FIG item monster-status {name}: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG item monster-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
