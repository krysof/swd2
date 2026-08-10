#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 42 / support selector 1eh."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "f655da9257b07c0386c901f4323bcd9ac7860b289ad36c4c9495036cf9e9a5b1"


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
        word(data, actor + 0x08, 0x0200)  # selector clears this bit only
        word(data, actor + 0x2D, 500)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 100)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 42)     # 邪中藥經, selector 1eh
        for offset in range(0x3E6, 0x3F0, 2):
            word(data, offset, 2)        # class-five payment leaves one each
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-1e save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-1e fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-1e fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
