#!/usr/bin/env python3
"""Create the deterministic release-format save used by the shop replay."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


RELEASE_SHA256 = {
    "SAVE.DAQ": "ba9a6fef7ff12697cfd72e3197125a18995fd78388f766b18f508f3f08f49099",
    "MAPZ.DA1": "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917",
    "NAME1.DSK": "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba",
}


def set_word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def set_string(data: bytearray, offset: int, capacity: int, value: str) -> None:
    encoded = value.encode("ascii")
    if len(encoded) >= capacity:
        raise ValueError(f"DOS path does not fit at {offset:#x}")
    data[offset:offset + capacity] = encoded + bytes(capacity - len(encoded))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    for name, expected in RELEASE_SHA256.items():
        actual = hashlib.sha256((args.game / name).read_bytes()).hexdigest()
        if actual != expected:
            parser.error(f"{name} is not the supported release file: {actual}")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    shutil.copy2(args.game / "MAPZ.DA1", args.output / "MAPZ.DA1")
    shutil.copy2(args.game / "NAME1.DSK", args.output / "NAME1.DSK")

    save = bytearray((args.game / "SAVE.DAQ").read_bytes())
    if len(save) != 0x546:
        parser.error("release SAVE.DAQ size changed")
    for slot in range(50):
        set_word(save, 0x382 + slot * 2, 0)
    for counter in range(5):
        set_word(save, 0x3e6 + counter * 2, 0)
    set_word(save, 0x104, 1000)

    # MAPZ directory 46 is SWRO7.  Entity zero stands at world (47,11) and
    # dispatches CHNA1 byte offset 58 (directory entry 29), whose opcode-19
    # inventory has eight entries.  Put the actor one cell to its left.
    set_word(save, 0x424, 46)
    set_word(save, 0x40f, 8)
    set_word(save, 0x413, 40)
    set_word(save, 0x415, 25)
    set_word(save, 0x417, 64)
    set_word(save, 0x419, 38)
    set_word(save, 0x41b, 20)
    set_word(save, 0x41d, 0)
    set_word(save, 0x40d, 48)
    set_word(save, 0x012, 50)
    set_word(save, 0x02a, 72)
    set_word(save, 0x0a2, 9)
    # RPG overwrites the historical drive letter with the current drive.  The
    # prefix must still occupy its original two bytes in original captures.
    set_string(save, 0x42d, 22, r"E:\SWD2\T1\SWRO7.RS4")
    set_string(save, 0x443, 22, r"E:\SWD2\T1\SWRO7.RRO")
    set_string(save, 0x459, 22, r"E:\SWD2\RX\TO01.RIX")
    set_string(save, 0x46f, 22, "E:CHNA1.EXE")
    set_string(save, 0x485, 24, "E:CHNA1.DSK")
    (args.output / "SAVE.DA1").write_bytes(save)
    print("RPG shop fixture: SWRO7/CHNA1 entry 29, eight goods, 1000 money")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
