#!/usr/bin/env python3
"""Create an IF fixture that installs AE then uses ability 88 / effect 36h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "0a0bcee91ebd77def7238d40a3d9e26553f8baea5faa7d9e29b4195d87f9f1dc"


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
        word(data, actor + 0x2D, 5000)   # survive both following monster actions
        word(data, actor + 0x2F, 5000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # class-four AP pays 10 then 120
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        for offset in range(0x382, 0x3E6, 2):
            word(data, offset, 0)
        data[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
        data[actor + 0x6D] = 51  # 祭雷符, selector 31h: install AE
        data[actor + 0x6E] = 88  # 散冰風輪符, effect 36h; requires AE

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-36 save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-36 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-36 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
