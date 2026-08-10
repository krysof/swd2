#!/usr/bin/env python3
"""Create a deterministic IF fixture for reachable ability 56 / effect 64h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "dc84bb7c224bc5c937389049941c1971b180eb5a4ce193a64fbfc322dd4a731e"


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
        word(data, 0x4A0, 0x04D4)
        word(data, actor + 0x08, 0)
        word(data, actor + 0x0E, 60000)
        word(data, actor + 0x2D, 60000)
        word(data, actor + 0x2F, 60000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 0xFFFF)
        data[actor + 0x6D] = 56          # 霧縛符, effect 64h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-64-status save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-64-status fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-64-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
