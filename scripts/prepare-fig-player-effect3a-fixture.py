#!/usr/bin/env python3
"""Create a deterministic IF fixture for reachable ability 77 / effect 3ah."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "4b7aa1be75b40b8b13753deabc513d90e567ef144900368d2ba59aa030830611"


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
        word(data, actor + 0x08, 0)
        word(data, actor + 0x2D, 5000)   # survive the following monster action
        word(data, actor + 0x2F, 5000)
        word(data, actor + 0x35, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)   # class-four cost 22 is available
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 77)     # 驚鬼符, effect 3ah
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-3a save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-3a fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-3a fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
