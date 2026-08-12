#!/usr/bin/env python3
"""Stage the shipped AREA1 east-scroll starting position."""

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

        # This route intentionally uses the released slot-one state verbatim.
        # DA1/1 are consumed by the rewrite; the Q triplet lets RPGOC.COM enter
        # the untouched RPG.EXE through the same OC load boundary.
        for name in ("SAVE.DA1", "SAVE.DAQ"):
            shutil.copy2(args.game / "SAVE.DA1", args.output / name)
        for source, destinations in (
                ("MAPZ.DA1", ("MAPZ.DA1", "MAPZ.DAQ")),
                ("NAME1.DSK", ("NAME1.DSK", "NAMEQ.DSK"))):
            for destination in destinations:
                shutil.copy2(args.game / source, args.output / destination)
        print("RPG AREA1 east-scroll fixture: released slot one")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG AREA1 east-scroll fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
