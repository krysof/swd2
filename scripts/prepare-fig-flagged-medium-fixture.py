#!/usr/bin/env python3
"""Create the deterministic IF-entry target-flag missing-medium fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "364ecc89b3ff4bc4414b188d226f02e9774bbc66f667ca5ceb33ec91d2ad0c62"


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
        word(data, actor + 0x2D, 1000)
        word(data, actor + 0x2F, 1000)
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 1000)
        word(data, actor + 0x6D, 86)  # effect 63, low-byte AG requirement
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG flagged-medium fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG flagged-medium fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG flagged-medium fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
