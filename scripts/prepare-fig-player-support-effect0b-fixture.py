#!/usr/bin/env python3
"""Create a deterministic IF fixture for ability 103 / support selector 0bh."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "96de3965178abcc3e1f475f562976cec8959f8f38f49e7b066c7467fc63b2394"


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
        word(data, actor + 0x35, 100)    # 70% plus wisdom/4 becomes 815
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # class-four cost 50 is deferred
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 103)    # 順脈符, selector 0bh
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-support-0b save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-support-0b fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-support-0b fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
