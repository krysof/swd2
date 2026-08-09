#!/usr/bin/env python3
"""Create a deterministic one-hit, exactly-one-level FIG fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "96e1cc1b75e5cd5b454f58bc8aa4d338bc34592169e4d825dc8760e85fbcdc91"


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
        word(data, actor + 8, 0)
        word(data, actor + 0x0C, 30000)
        word(data, actor + 0x0E, 0)
        word(data, actor + 0x2D, 1000)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x31, 9)
        word(data, actor + 0x33, 1)
        # The single enemy grants 132 experience.  One existing point reaches
        # the shipped level-nine threshold 133 exactly, producing one page.
        word(data, actor + 0x39, 1)
        word(data, actor + 0x3B, 133)
        word(data, actor + 0x41, 30000)
        word(data, actor + 0x43, 0)
        word(data, actor + 0x5D, 30000)
        word(data, actor + 0x5F, 30000)
        word(data, actor + 0x65, 0)
        word(data, actor + 0x67, 0)
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG level-up fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG level-up fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG level-up fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
