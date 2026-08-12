#!/usr/bin/env python3
"""Stage either remaining shipped FIG generic-monster flash colour class."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


VARIANTS = {
    "color81": (36, "4aad9a8910ca21ee0931e29fd43c1968f041302311add0b876cf352a565dbf19"),
    "color5c": (548, "25fef85ea2c97c4f04c7d7e364032388edd23758b33aede2228d63064afd3471"),
}


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("variant", choices=VARIANTS)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        for name in ("MAPZ.DA1", "NAME1.DSK"):
            shutil.copy2(args.game / name, args.output / name)
        directory_offset, expected = VARIANTS[args.variant]
        data = bytearray((args.game / "SAVE.DA1").read_bytes())
        word(data, 0x10, 1)
        word(data, 0x4A0, directory_offset)
        word(data, 0x49C, 0x1002)
        word(data, 0x49E, 20)
        word(data, 0x3F2, 1)
        for index in range(4):
            actor = 0x106 + index * 0x9F
            hit_points = 60000 if index == 0 else 0
            for offset, value in (
                    (0x08, 0), (0x0C, 0), (0x0E, 1000),
                    (0x2D, hit_points), (0x2F, hit_points),
                    (0x31, 1), (0x33, 1),
                    (0x41, 0), (0x43, 1000), (0x5D, 0), (0x5F, 0),
                    (0x65, 0), (0x67, 0)):
                word(data, actor + offset, value)
            for offset in (0x27, 0x28, 0x2A, 0x2B):
                data[actor + offset] = 0
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise ValueError(
                f"FIG generic flash {args.variant} fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG generic flash {args.variant} fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG generic flash fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
