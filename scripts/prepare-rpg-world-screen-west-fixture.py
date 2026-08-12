#!/usr/bin/env python3
"""Stage AREA1 for the original westward screen step on-screen movement route."""

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
EXPECTED_SAVE_SHA256 = "35cd4402edced78df412d32903f3cfe3252b7b37da0826e65669005be36756ce"


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
        if len(save) != 0x546 or int.from_bytes(save[0x40D:0x40F], "little") != 580:
            raise ValueError("release AREA1 position boundary changed")
        # Start one horizontal unit east of the centre anchor at the east
        # viewport limit. Left returns screen X 28h to 26h without scrolling.
        set_word(save, 0x41B, 140)
        set_word(save, 0x41D, 0)
        set_word(save, 0x40D, 288)
        set_word(save, 0x12, 40)
        actual = hashlib.sha256(save).hexdigest()
        if actual != EXPECTED_SAVE_SHA256:
            raise ValueError(f"screen-west SAVE differs: {actual}")
        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for source, destinations in (
                ("MAPZ.DA1", ("MAPZ.DA1", "MAPZ.DAQ")),
                ("NAME1.DSK", ("NAME1.DSK", "NAMEQ.DSK"))):
            for destination in destinations:
                shutil.copy2(args.game / source, args.output / destination)
        print(f"RPG AREA1 screen-west fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG AREA1 screen-west fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
