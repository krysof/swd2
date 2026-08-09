#!/usr/bin/env python3
"""Create deterministic IF-entry fixtures for four player status cards."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


ABILITY_IDS = (35, 38, 33, 37)
EXPECTED = {
    35: "2d59f72ee7242ab46fe31056afd6ec27810ffb1c774ff9ad7cf0805489c95218",
    38: "2e773edf0ab5ecf8e4a782303c60248859fbcd7ea94fecb9de8444620a7b74cb",
    33: "dbcdb4ca7f8e848c3889f0d2eed2127634bfbe998ce50115cf4ed1f64bd17626",
    37: "0357f3e411a57205abccd0b2a1a8b362dd5157dd250e79ce71be8c263816ead7",
}


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
        original = bytearray((args.game / "SAVE.DA1").read_bytes())
        actor = 0x106
        for ability_id in ABILITY_IDS:
            case = args.output / f"ability-{ability_id}"
            case.mkdir()
            for name in ("MAPZ.DA1", "NAME1.DSK"):
                shutil.copy2(args.game / name, case / name)
            data = bytearray(original)
            word(data, 0x10, 1)             # one visible/commandable actor
            word(data, 0x4A0, 392)          # empty-introduction formation
            word(data, actor + 0x2D, 1000)  # HP/current maximum
            word(data, actor + 0x2F, 1000)
            word(data, actor + 0x35, 1000)  # class-3 current/maximum
            word(data, actor + 0x37, 1000)
            word(data, actor + 0x55, 1000)  # AP current/maximum
            word(data, actor + 0x57, 1000)
            word(data, actor + 0x5D, 1000)  # act before the monster
            word(data, actor + 0x6D, ability_id)
            actual = hashlib.sha256(data).hexdigest()
            if actual != EXPECTED[ability_id]:
                raise ValueError(
                    f"FIG player-status ability {ability_id} differs: {actual}")
            (case / "SAVE.DA1").write_bytes(data)
            print(f"FIG player-status ability {ability_id}: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
