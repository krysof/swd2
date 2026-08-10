#!/usr/bin/env python3
"""Create the IF fixture for learned abilities 51 then 53 on medium AE."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "71a0d01537b4582f6aa1e957941c272d2cc7a37bb60ede744e7ec77ae29b8f16"


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
        word(data, actor + 0x2D, 1000)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        for offset in range(0x382, 0x3E6, 2):
            word(data, offset, 0)
        data[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
        data[actor + 0x6D] = 51  # selector 31h: install AE
        data[actor + 0x6E] = 53  # selector 3dh: dismiss AE

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG learned-medium save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG learned-medium fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG learned-medium fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
