#!/usr/bin/env python3
"""Create the IF-entry fixture for item 242's class-5 embedded record."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "1de031192649fba188adc2d210450ed82c4deea894af3b85c96d2fcd20597432"


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
        word(data, 0x10, 1)
        word(data, 0x4A0, 392)
        word(data, actor + 0x08, 0)
        word(data, actor + 0x2D, 100)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        for offset in range(0x382, 0x3E6, 2):
            word(data, offset, 0)
        word(data, 0x382, 242)          # 渾氣符, ability 102/effect 0ah
        for index in range(5):
            # Deliberately nonzero: the embedded learned record is class 5,
            # but the direct ITEM path must leave these counters untouched.
            word(data, 0x3E6 + index * 2, 2)

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG class-5 item-support save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG class-5 item-support fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG class-5 item-support fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
