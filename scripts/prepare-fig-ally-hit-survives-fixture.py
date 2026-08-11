#!/usr/bin/env python3
"""Create the deterministic captured-ally nonlethal physical-hit fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "6766bb67135665e1950190d25a96e7498da2a3f972a5d1a1616ed710cce2186f"


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
        word(data, 0x10, 1)              # one commandable actor
        word(data, 0x4A0, 288)           # one 159-HP monster survives 146 damage
        word(data, 0x49C, 0x1000)        # ally physical hit, then monster action
        word(data, 0x382, 417)           # captured item "化民"
        word(data, actor + 0x0C, 1)      # harmless player attack
        word(data, actor + 0x0E, 60000)  # survive through the next command page
        word(data, actor + 0x2D, 60000)
        word(data, actor + 0x2F, 60000)
        word(data, actor + 0x31, 1)
        word(data, actor + 0x33, 60000)
        word(data, actor + 0x35, 1000)   # pay captured ally level*2
        word(data, actor + 0x37, 1000)
        word(data, actor + 0x41, 1)
        word(data, actor + 0x43, 60000)
        word(data, actor + 0x55, 1000)
        word(data, actor + 0x57, 1000)
        word(data, actor + 0x5D, 60000)
        word(data, actor + 0x5F, 60000)
        word(data, actor + 0x65, 0)
        word(data, actor + 0x67, 0)

        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(
                f"FIG captured-ally surviving-hit fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG captured-ally surviving-hit fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG captured-ally surviving-hit fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
