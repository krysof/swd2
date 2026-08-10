#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 94 / support selector 04h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "7d3727bec49f2054ac8242735565814850f73a960d60ecda469ff8ade907953b"


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
        word(data, actor + 0x2D, 500)    # selector 04h visibly fills both bars
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # enough ability resource for cost 200
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)   # player acts before the monster
        word(data, actor + 0x6D, 94)     # 反陰補天符, selector 04h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-04 save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-04 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-04 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
