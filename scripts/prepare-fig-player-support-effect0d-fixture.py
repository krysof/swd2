#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 112 / support selector 0dh."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "b26f9957bffef53dbe950558555bb8d3107e736be6e84fd9638635835c03c2d4"


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
        word(data, actor + 0x08, 0)
        word(data, actor + 0x2D, 100)    # both 45% bars become 565
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 100)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # class-one cost 100 is deferred
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 112)    # 鬼神聚元法, selector 0dh
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-0d save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-0d fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-0d fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
