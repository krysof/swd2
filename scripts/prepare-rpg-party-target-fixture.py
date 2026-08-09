#!/usr/bin/env python3
"""Create the deterministic four-member save used by the party-card replay."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


FILES = {
    "SAVE.DAQ": "ba9a6fef7ff12697cfd72e3197125a18995fd78388f766b18f508f3f08f49099",
    "MAPZ.DA1": "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917",
    "NAME1.DSK": "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba",
}


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def string(data: bytearray, offset: int, capacity: int, value: str) -> None:
    encoded = value.encode("ascii")
    if len(encoded) >= capacity:
        raise ValueError(f"DOS path does not fit at {offset:#x}")
    data[offset:offset + capacity] = encoded + bytes(capacity - len(encoded))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    for name, expected in FILES.items():
        actual = hashlib.sha256((args.game / name).read_bytes()).hexdigest()
        if actual != expected:
            parser.error(f"{name} is not the supported release file: {actual}")
    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    shutil.copy2(args.game / "MAPZ.DA1", args.output / "MAPZ.DA1")
    shutil.copy2(args.game / "NAME1.DSK", args.output / "NAME1.DSK")

    save = bytearray((args.game / "SAVE.DAQ").read_bytes())
    for slot in range(50):
        word(save, 0x382 + slot * 2, 0)
    word(save, 0x10, 4)
    word(save, 0x104, 1000)
    word(save, 0x424, 46)
    word(save, 0x40f, 8)
    word(save, 0x413, 40)
    word(save, 0x415, 25)
    word(save, 0x417, 64)
    word(save, 0x419, 38)
    word(save, 0x41b, 20)
    word(save, 0x41d, 0)
    word(save, 0x40d, 48)
    word(save, 0x012, 50)
    word(save, 0x02a, 72)
    word(save, 0x0a2, 9)
    string(save, 0x42d, 22, r"E:\SWD2\T1\SWRO7.RS4")
    string(save, 0x443, 22, r"E:\SWD2\T1\SWRO7.RRO")
    string(save, 0x459, 22, r"E:\SWD2\RX\TO01.RIX")
    string(save, 0x46f, 22, "E:CHNA1.EXE")
    string(save, 0x485, 24, "E:CHNA1.DSK")
    (args.output / "SAVE.DA1").write_bytes(save)
    print("RPG party target fixture: four members and an empty equipment row")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
