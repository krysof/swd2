#!/usr/bin/env python3
"""Create a deterministic IF fixture for reachable ability 6 / effect 5eh resisted path."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "026a60d93c4f6125b50469c18206fbc72e23d83a2554d0edae6a56a6c3250c64"


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
        word(data, actor + 0x35, 1000)   # class-two cost 45 is available
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 6)      # 攝魂大法, effect 5eh
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-5e-resistance save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-5e-resistance fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-5e-resistance fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
