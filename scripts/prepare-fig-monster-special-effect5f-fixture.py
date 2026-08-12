#!/usr/bin/env python3
"""Stage shipped definition 514's successful 26af effect-5F route."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "de7755c7c8c350c87c18552492f736c780a8119f7391cf441d808e723e4b88da"


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
        word(data, 0x4A0, 46)      # one shipped definition-514 monster
        word(data, 0x49C, 0x1010)  # special-A ability 139, duration two
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
            for offset in (0x27, 0x28, 0x2A, 0x2B):
                data[actor + offset] = 0
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG special-effect-5F fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG special-effect-5F fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG special-effect-5F fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
