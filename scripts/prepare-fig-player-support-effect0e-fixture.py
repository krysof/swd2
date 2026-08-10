#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 113 / support selector 0eh."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "f04580f31399f11945eb1443f9b05b4bd8b2869dcaa333fa11a7033db7e59e6b"


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
        word(data, actor + 0x2D, 100)    # 70% HP becomes 815
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 100)    # 100% second resource reaches cap
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # class-one cost 100 is deferred
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 113)    # 八方歸元術, selector 0eh
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-0e save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-0e fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-0e fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
