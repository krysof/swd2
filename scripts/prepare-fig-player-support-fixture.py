#!/usr/bin/env python3
"""Create a deterministic IF-entry fixture for FIG support selector 01h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "95d265bc03bfd64c0de15daeff0aafadf2cc3f4b784ec5ceeb0bebdd1e970bdf"


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
        word(data, actor + 0x2D, 100)    # low HP exposes the before card
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)   # secondary current/maximum
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # ability resource current/maximum
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)   # player acts before the monster
        word(data, actor + 0x6D, 50)     # ability 50, selector 01h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
