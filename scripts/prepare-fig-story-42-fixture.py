#!/usr/bin/env python3
"""Create a deterministic one-hit directory-42 story-boss fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "bce3144449abee216c3ff01e3aaf0af34431c591b7e6537ad4c53d4558f39275"


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
        word(data, 0x4A0, 0x42)
        for offset, value in (
                (8, 0), (0x0C, 100), (0x0E, 1000),
                (0x2D, 1000), (0x2F, 1000), (0x31, 30),
                (0x33, 1000), (0x41, 100), (0x43, 1000),
                (0x5D, 1000), (0x5F, 1000),
                (0x65, 1000), (0x67, 1000)):
            word(data, actor + offset, value)
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG story-42 fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG story-42 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG story-42 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
