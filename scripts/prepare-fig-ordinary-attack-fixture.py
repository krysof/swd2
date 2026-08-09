#!/usr/bin/env python3
"""Create the deterministic directory-64h ordinary-attack collection fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "554a111074f0e525e9493dca38aeea0464294a328254c606919df9eb403f9847"


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
        word(data, 0x10, 3)
        word(data, 0x4A0, 100)
        word(data, 0x49C, 0x1004)
        word(data, 0x49E, 20)
        word(data, 0x3F2, 1)
        for index in range(3):
            actor = 0x106 + index * 0x9F
            for offset, value in (
                    (8, 0), (0x0C, 30000), (0x0E, 30000),
                    (0x2D, 1000), (0x2F, 1000), (0x31, 1000),
                    (0x33, 1), (0x41, 30000), (0x43, 30000),
                    (0x5D, 30000), (0x5F, 30000),
                    (0x65, 0), (0x67, 0)):
                word(data, actor + offset, value)
        word(data, 0x382 + 49 * 2, 0)
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG ordinary-attack fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG ordinary-attack fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG ordinary-attack fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
