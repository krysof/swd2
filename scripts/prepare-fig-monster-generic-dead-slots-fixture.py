#!/usr/bin/env python3
"""Stage ability 74 with only physical party slots zero and two alive."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "7514130a958b38ad6e06d14cfc8f1b715c366bd802d233ca52bb474331c0f9d5"


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
        word(data, 0x10, 4)
        word(data, 0x4A0, 734)     # one shipped definition-334 monster
        word(data, 0x49C, 0x1004)  # selects generic ability 74
        word(data, 0x49E, 20)
        word(data, 0x3F2, 1)
        for index in range(4):
            actor = 0x106 + index * 0x9F
            dead = index in (1, 3)
            for offset, value in (
                    (0x08, 0x2000 if dead else 0),
                    (0x0C, 0), (0x0E, 0),
                    (0x2D, 0 if dead else 1), (0x2F, 60000),
                    (0x31, 1), (0x33, 1), (0x41, 0), (0x43, 0),
                    (0x5D, 0), (0x5F, 0), (0x65, 0), (0x67, 0)):
                word(data, actor + offset, value)
            for offset in (0x27, 0x28, 0x2A, 0x2B):
                data[actor + offset] = 0
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(
                f"FIG generic dead-slot fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG generic dead-slot fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG generic dead-slot fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
