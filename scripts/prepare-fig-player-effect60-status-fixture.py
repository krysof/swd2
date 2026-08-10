#!/usr/bin/env python3
"""Create a deterministic IF fixture for reachable ability 93 / effect 60h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "d4e028f693215cc47d8ffa412c023162763ae9c0f5386b08778b21d2f68ba77a"


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
        word(data, actor + 0x55, 1000)  # class-four cost 20 is available
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 0xFFFF)
        data[actor + 0x6D] = 93          # 五毒符, effect 60h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-60-status save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-60-status fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-60-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
