#!/usr/bin/env python3
"""Create the deterministic IF-entry fixture for type-10 support item 190."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "df90801bf76307381ceaf6db9616d7cbbf395aca1557c53c8ee9e8ae51be2149"


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
        word(data, 0x4A0, 392)           # one monster; no introduction
        word(data, actor + 0x08, 0)      # clear status/death bits
        word(data, actor + 0x2D, 100)    # low HP exposes selector 01h
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # full AP exposes the 7-point debit
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)   # player acts before the monster
        for offset in range(0x382, 0x3E6, 2):
            word(data, offset, 0)
        word(data, 0x382, 190)           # 氣聚符, type-10 ability 50/effect 01h

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG item-support save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG item-support fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG item-support fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
