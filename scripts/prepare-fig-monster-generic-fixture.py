#!/usr/bin/env python3
"""Create the deterministic directory-188h monster generic-ability fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "4cf1b2a77f0455b8cd7c45acec67e9a296728eabf6cd45f11db4265f420d2de0"


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
        word(data, 0x10, 1)
        word(data, 0x4A0, 392)     # one definition-500 monster
        word(data, 0x49C, 0x101E)  # ability 10 after the five initiative rolls
        word(data, 0x49E, 20)
        word(data, 0x3F2, 1)
        for index in range(4):
            actor = 0x106 + index * 0x9F
            for offset, value in (
                    (0x08, 0), (0x0C, 0), (0x0E, 1000),
                    (0x2D, 1000), (0x2F, 1000), (0x31, 1), (0x33, 1),
                    (0x41, 0), (0x43, 1000), (0x5D, 0), (0x5F, 0),
                    (0x65, 0), (0x67, 0)):
                word(data, actor + offset, value)
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG monster-generic fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG monster-generic fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG monster-generic fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
