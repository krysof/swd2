#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 111 / support selector 07h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "9241f8edc1674ec2c32fc3c66845e4c5ed2ee7bc2d5a5fd0f2c59fb22d3151b6"


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
        word(data, actor + 0x2D, 500)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 0xFFFF)  # pays the shipped 0x8fff cost
        word(data, actor + 0x37, 0xFFFF)
        word(data, actor + 0x55, 100)     # selector 07h restores this bar
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 111)     # 靈神重生, selector 07h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-07 save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-07 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-07 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
