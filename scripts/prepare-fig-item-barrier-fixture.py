#!/usr/bin/env python3
"""Create the deterministic IF-entry fixture for direct barrier item 204."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "64e540c9d24aeb314c45a0ee9c3d0312164e3bdc0017e260580b1a3c60c33e2f"


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
        word(data, 0x4A0, 0x4D4)
        word(data, actor + 0x08, 0)
        word(data, actor + 0x0E, 60000)
        word(data, actor + 0x2D, 60000)
        word(data, actor + 0x2F, 60000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 0xFFFF)
        for offset in range(0x382, 0x3E6, 2):
            word(data, offset, 0)
        word(data, 0x382, 204)  # 代形符, direct selector 62h

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG item-barrier save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG item-barrier fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG item-barrier fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
