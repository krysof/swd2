#!/usr/bin/env python3
"""Create the deterministic two-monster FIG periodic-zero fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "24e7772aa6d4088431a245a3782eb479ab960065061d2a8862149bb020575e87"


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
        word(data, 0x4A0, 1240)          # two-monster record at ORC data 1328
        word(data, actor + 0x0E, 60000)  # survive both enemy turns
        word(data, actor + 0x2D, 60000)
        word(data, actor + 0x2F, 60000)
        word(data, actor + 0x31, 1)      # periodic modulus is two
        word(data, actor + 0x33, 1)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 30000)  # install slot two before monster zero
        word(data, actor + 0x5F, 30000)
        data[actor + 0x6D] = 48          # selector 60h / periodic slot two
        for slot in range(5):
            word(data, 0x3E6 + slot * 2, 5)
        # FIG reads code words rather than using a conventional PRNG. Cursor
        # 1004h makes the installed slot's first random(2) result literal zero.
        word(data, 0x49C, 0x1004)

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG periodic-zero fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG periodic-zero fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG periodic-zero fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
