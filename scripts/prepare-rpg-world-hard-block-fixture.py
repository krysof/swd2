#!/usr/bin/env python3
"""Stage the shipped AREA1 hard east-wall collision position."""

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
    "2cc5f73a8f119c2814ad31b4bbffcb6b856ad44681174a1dd8d67aea4269f510"


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
        # Keep screen (26h,50h), place the viewport at (100,0), and therefore
        # place the party at world (120,12).  East probes (122,12)=8013h;
        # both corner alternatives are blocked at (121,13)=8383h and
        # (121,11)=808eh, so RPG:1a42..1e63 changes facing/animation without
        # moving the actor or viewport.
        set_word(save, 0x41B, 100)
        set_word(save, 0x41D, 0)
        set_word(save, 0x40D, 208)
        actual = hashlib.sha256(save).hexdigest()
        if actual != EXPECTED_SAVE_SHA256:
            raise ValueError(f"hard-block SAVE differs: {actual}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for source, destinations in (
                ("MAPZ.DA1", ("MAPZ.DA1", "MAPZ.DAQ")),
                ("NAME1.DSK", ("NAME1.DSK", "NAMEQ.DSK"))):
            for destination in destinations:
                shutil.copy2(args.game / source, args.output / destination)
        print(f"RPG AREA1 hard-block fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG AREA1 hard-block fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
