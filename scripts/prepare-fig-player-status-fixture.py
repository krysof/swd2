#!/usr/bin/env python3
"""Create the deterministic IF-entry learned-support status-card fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "2d59f72ee7242ab46fe31056afd6ec27810ffb1c774ff9ad7cf0805489c95218"


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
        word(data, 0x10, 1)             # one visible/commandable party actor
        word(data, 0x4A0, 392)          # empty-introduction formation
        word(data, actor + 0x2D, 1000)  # HP/current maximum
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x35, 1000)  # class-3 resource/current maximum
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)  # AP/current maximum
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)  # act before the monster
        word(data, actor + 0x6D, 35)    # learned defense-increase ability
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-status fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-status fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
