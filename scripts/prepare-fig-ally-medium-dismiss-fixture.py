#!/usr/bin/env python3
"""Create the captured-ally special-B AF-dismissal fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "5e184afc536afa51364f6d30ea70d69c9d592fc38245cd892ff54a13669ca6c0"


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
        word(data, 0x4A0, 392)       # fixed one-monster battle
        word(data, 0x49C, 0x1004)    # item 394 selects special B ability 67
        word(data, 0x382, 394)
        for offset in range(0x384, 0x3E6, 2):
            word(data, offset, 0)
        for offset, value in (
                (0x0C, 1), (0x0E, 60000), (0x2D, 60000),
                (0x2F, 60000), (0x31, 1), (0x33, 60000),
                (0x35, 1000), (0x37, 1000), (0x41, 1),
                (0x43, 60000), (0x55, 1000), (0x57, 1000),
                (0x5D, 60000), (0x5F, 60000), (0x65, 0), (0x67, 0)):
            word(data, actor + offset, value)
        data[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
        data[actor + 0x6D] = 66       # selector 3bh installs AF first

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(
                f"FIG captured-ally medium-dismiss fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG captured-ally medium-dismiss fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG captured-ally medium-dismiss fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
