#!/usr/bin/env python3
"""Create FIG's zero-selector, implicit-monster learned-damage fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "5ad86b88cdad71c37836a6ab8ba3efa7022ecf256af3c981ec1aaad9c2882178"


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
        word(data, actor + 0x0E, 60000)
        word(data, actor + 0x2D, 60000)
        word(data, actor + 0x2F, 60000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 0xFFFF)
        data[actor + 0x6D] = 100       # target mode zero, effect 54h
        data[actor + 0x6E] = 0
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG implicit-damage save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG implicit-damage fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG implicit-damage fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
