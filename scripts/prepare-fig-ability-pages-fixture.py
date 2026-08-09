#!/usr/bin/env python3
"""Create deterministic IF-entry saves for FIG's five ability resource cards."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


ABILITY_IDS = (1, 4, 33, 3, 41)
EXPECTED = {
    1: "7b90ec8fc477d50dc24e75b22619441ef745d612e95d43240d3f08558ad5e835",
    4: "6ba1432582389891027fd4944dc109114d41e0f65387f1518b68a96bec714214",
    33: "b6628b77b22e53320678bcaee3d7fc0575febdadbac73a440b26088f66c577dd",
    3: "f67fe80ab88f2f9eca23b26e00605b18a78860f9e8a43b92d39f5cc5d29122af",
    41: "8c0eb619d6f2c1670a7eb3244bbd8ee7e9b7497faabd6f447b8513bd36674df7",
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
            word(data, 0x4A0, 392)       # empty-introduction battle formation
            word(data, actor + 0x35, 1000)
            word(data, actor + 0x55, 1000)
            data[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
            data[actor + 0x6D] = ability_id
            for offset in range(0x3E6, 0x3F0, 2):
                word(data, offset, 1)    # all five class-five elements present
            actual = hashlib.sha256(data).hexdigest()
            expected = EXPECTED.get(ability_id)
            if expected and actual != expected:
                raise ValueError(
                    f"ability {ability_id} fixture differs: {actual}")
            (case / "SAVE.DA1").write_bytes(data)
            print(f"FIG ability {ability_id} fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG ability-page fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
