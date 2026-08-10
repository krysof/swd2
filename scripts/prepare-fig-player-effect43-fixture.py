#!/usr/bin/env python3
"""Create an IF fixture that installs AF then uses ability 87 / effect 43h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "3e9b004de9a933482e0c0710f14d96323da7b0816ef18d1eb2872cf58b931325"


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
        word(data, actor + 0x55, 1000)   # class-four AP pays 12 then 110
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        for offset in range(0x382, 0x3E6, 2):
            word(data, offset, 0)
        data[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
        data[actor + 0x6D] = 66  # 祭火符, selector 3bh: install AF
        data[actor + 0x6E] = 87  # 帶火風輪符, effect 43h; requires AF

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-43 save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-43 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-43 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
