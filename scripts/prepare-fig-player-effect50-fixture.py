#!/usr/bin/env python3
"""Create a deterministic IF fixture for reachable ability 17 / effect 50h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "adefbdb821025dae5eeccc23975d84e7b86daf933f1c11b9e055149155d06cc6"


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
        word(data, actor + 0x35, 1000)   # class-two cost 68 is available
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 17)     # 驚虎吞狗掌, effect 50h
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG player-effect-50 save differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG player-effect-50 fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-50 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
