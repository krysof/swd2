#!/usr/bin/env python3
"""Stage a deterministic physical attack with ITEM 62 equipped."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "2c19468b791cbcb7bd426c17373124be02105362a0e1aba66cc3f0786f815371"
EXPECTED_ITEM = "73009791df640671791d0daa5a1335eb4a491f0b37848b481f98719e2785c70e"
EXPECTED_FAN = "7b90e4340d032f6902095122763a8a4dfaa45d38eeb59309ede4151b98e43f9b"


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        tianhuo = (args.output / "SW" / "SW062.RSK").read_bytes()
        iron_fan = (args.output / "SW" / "SW124.RSK").read_bytes()
        if tianhuo != iron_fan or sha256(tianhuo) != EXPECTED_FAN:
            raise ValueError("SW062 is not the locked SW124 repair copy")

        save_path = args.output / "SAVE.DA1"
        save = bytearray(save_path.read_bytes())
        actor = 0x106
        word(save, 0x10, 1)
        word(save, 0x4A0, 392)
        word(save, 0x49E, 0xFFFF)
        word(save, actor, 36)       # Gu Yuesheng, the allowed wielder.
        word(save, actor + 0x0C, 30000)
        word(save, actor + 0x14, 62)
        word(save, actor + 0x16, 0)
        save[actor + 0x2C] = 0
        for offset, value in (
                (0x2D, 1000), (0x2F, 1000), (0x31, 100), (0x33, 1),
                (0x41, 30000), (0x5D, 30000), (0x5F, 30000)):
            word(save, actor + offset, value)
        # A physical attack must not require or consume any elemental medium.
        for offset in range(0x3E6, 0x3F0, 2):
            word(save, offset, 0)

        item_path = args.output / "ITEM.EXE"
        item = bytearray(item_path.read_bytes())
        header_size = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header_size:]
        record_index = 500 + 2
        record = int.from_bytes(
            image[record_index * 2:record_index * 2 + 2], "little")
        item[header_size + record + 0x16] = 0
        for offset, value in (
                (0x26, 0), (0x2C, 60000), (0x2E, 0), (0x32, 0),
                (0x38, 1000), (0x3A, 0), (0x3E, 0), (0x42, 0),
                (0x44, 0), (0x46, 0), (0x4C, 0)):
            word(item, header_size + record + offset, value)

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE or item_digest != EXPECTED_ITEM:
            raise ValueError(
                f"fixture differs: SAVE={save_digest} ITEM={item_digest}")
        save_path.write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG Tianhuo-fan fixture: "
            f"SAVE={save_digest} ITEM={item_digest} SW062={EXPECTED_FAN}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG Tianhuo-fan fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
