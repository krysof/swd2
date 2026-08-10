#!/usr/bin/env python3
"""Create the deterministic directory-3c2h resisted 26af status fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "70ab6d8fb9b393673c12d8d60b5b29bb586ea3e02d949458d17c7cc19c7c7192"


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
        word(data, 0x10, 1)
        word(data, 0x4A0, 962)     # definitions 456 and 379
        word(data, 0x49C, 0x1000)  # 456 selects special-A ability 117
        word(data, 0x49E, 20)
        word(data, 0x3F2, 1)
        for index in range(4):
            actor = 0x106 + index * 0x9F
            for offset, value in (
                    (0x08, 0), (0x0C, 0), (0x0E, 1000),
                    (0x2D, 1000), (0x2F, 1000), (0x31, 1), (0x33, 1),
                    (0x41, 0), (0x43, 1000), (0x5D, 0), (0x5F, 0),
                    (0x65, 0), (0x67, 0)):
                word(data, actor + offset, value)
            # Clear all three magic resistances and the separate status byte;
            # actor zero's third resistance is enabled below.
            for offset in (0x27, 0x28, 0x2A, 0x2B):
                data[actor + offset] = 0
        # Effect 64h checks exactly this byte at 26e8..26f1 and skips 262f's
        # name card/status mutation when it equals one.
        data[0x106 + 0x2B] = 1
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG monster-special-resistance fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG monster-special-resistance fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG monster-special-resistance fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
