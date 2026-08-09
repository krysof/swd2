#!/usr/bin/env python3
"""Create a deterministic IF-entry one-hit party-defeat fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "34a62431053e18d275271ae472a955af5d4217eb58b399d450d814bae75aaf5c"


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
        word(data, actor + 0x0C, 0)
        word(data, actor + 0x0E, 0)
        word(data, actor + 0x2D, 1)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x31, 1)
        word(data, actor + 0x33, 1)
        word(data, actor + 0x41, 0)
        word(data, actor + 0x43, 0)
        word(data, actor + 0x5D, 1)
        word(data, actor + 0x5F, 1)
        word(data, actor + 0x65, 0)
        word(data, actor + 0x67, 0)
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG defeat fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG defeat fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG defeat fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
