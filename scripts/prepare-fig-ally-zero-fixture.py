#!/usr/bin/env python3
"""Create the deterministic captured-ally zero physical-damage fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "49268c0c118db1b6037614f9295a9ea8263acb3e9dbe23223c75481d3942d70c"


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
        for name in ("MAPZ.DA1", "NAME1.DSK"):
            shutil.copy2(args.game / name, args.output / name)

        data = bytearray((args.game / "SAVE.DA1").read_bytes())
        actor = 0x106
        word(data, 0x10, 1)              # one commandable actor
        word(data, 0x4A0, 100)           # intro/two-monster directory 64h
        word(data, 0x49C, 0x1026)        # summon, turns, targets, zero hit
        word(data, 0x382, 420)           # captured item “童女”
        word(data, actor + 0x0C, 1)      # player physical attack also rolls zero
        word(data, actor + 0x0E, 60000)  # survive both rounds
        word(data, actor + 0x2D, 60000)
        word(data, actor + 0x2F, 60000)
        word(data, actor + 0x31, 1)
        word(data, actor + 0x33, 60000)
        word(data, actor + 0x35, 1000)   # pay the captured ally's level*2
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x41, 1)
        word(data, actor + 0x43, 60000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 60000)
        word(data, actor + 0x5F, 60000)
        word(data, actor + 0x65, 0)
        word(data, actor + 0x67, 0)

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG captured-ally zero fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG captured-ally zero fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG captured-ally zero fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
