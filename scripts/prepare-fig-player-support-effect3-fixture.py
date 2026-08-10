#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 81 / support selector 03h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "d64f052375e7e675483790fdac8fd9a2526c17b8ea1a6dfbdbdbaef8e4f6da6c"


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
        word(data, actor + 0x2D, 500)    # selector 03h visibly fills both bars
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # enough ability resource for cost 30
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)   # player acts before the monster
        word(data, actor + 0x6D, 81)     # 結命符, selector 03h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-03 save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-03 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-03 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
