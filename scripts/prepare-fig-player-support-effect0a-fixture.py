#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 44 / support selector 0ah."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "20fd95ddaf484c6697e21986e35bcd9db7794f6503c823d05b0941baff87a95b"


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
        word(data, actor + 0x2D, 500)    # HP must remain unchanged
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 100)    # selector 0ah restores only this bar
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 500)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 44)     # 合精法, selector 0ah
        for offset in range(0x3E6, 0x3F0, 2):
            word(data, offset, 2)        # class-five payment leaves one each
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-0a save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-0a fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-0a fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
