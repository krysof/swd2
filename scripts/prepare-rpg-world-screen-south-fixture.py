#!/usr/bin/env python3
"""Stage AREA1 at its south viewport limit with a clear south step."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


RELEASE_SHA256 = {
    "SAVE.DA1": "9ca3a8029f4d9d67dffff728d3f8e8902409f94954f10d7a9a3e0c90d053007e",
    "MAPZ.DA1": "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917",
    "NAME1.DSK": "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba",
}
EXPECTED_SAVE_SHA256 = \
    "d48cf6420f67d4655ecd80fa67f00741beec4f7c86c90f0726505baf58f23bd4"


def set_word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        for name, expected in RELEASE_SHA256.items():
            actual = hashlib.sha256((args.game / name).read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError(f"{name} differs from the release: {actual}")
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or \
                int.from_bytes(save[0x41B:0x41D], "little") != 106 or \
                int.from_bytes(save[0x41D:0x41F], "little") != 1 or \
                int.from_bytes(save[0x40D:0x40F], "little") != 580:
            raise ValueError("release AREA1 position boundary changed")
        # AREA1 is 180 rows and the viewport is 25 rows, so 155 is the exact
        # south limit checked by RPG:1c4f.  World (20,167) has a clear
        # three-word south footprint.  Down therefore advances actor screen Y
        # from 50h to 58h through 1d06 while viewport Y remains 155.
        set_word(save, 0x41B, 0)
        set_word(save, 0x41D, 155)
        set_word(save, 0x40D, 55808)
        actual = hashlib.sha256(save).hexdigest()
        if actual != EXPECTED_SAVE_SHA256:
            raise ValueError(f"screen-south SAVE differs: {actual}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for source, destinations in (
                ("MAPZ.DA1", ("MAPZ.DA1", "MAPZ.DAQ")),
                ("NAME1.DSK", ("NAME1.DSK", "NAMEQ.DSK"))):
            for destination in destinations:
                shutil.copy2(args.game / source, args.output / destination)
        print(f"RPG AREA1 screen-south fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG AREA1 screen-south fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
