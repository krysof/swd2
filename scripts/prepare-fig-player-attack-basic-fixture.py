#!/usr/bin/env python3
"""Stage a fixed FIG battle whose nonfatal player attack is followed by a basic monster attack."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "253c831487a56c53875727cc06c4290886dc114657fcd212304f83eb13579da4"
EXPECTED_ITEM = "73009791df640671791d0daa5a1335eb4a491f0b37848b481f98719e2785c70e"


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

        save = bytearray((args.output / "SAVE.DA1").read_bytes())
        actor = 0x106
        word(save, 0x10, 1)
        word(save, 0x4A0, 392)
        word(save, 0x49E, 0xFFFF)
        word(save, actor + 0x0C, 30000)
        word(save, actor + 0x2D, 1000)
        word(save, actor + 0x2F, 1000)
        word(save, actor + 0x31, 100)
        word(save, actor + 0x33, 1)
        word(save, actor + 0x41, 30000)
        word(save, actor + 0x5D, 30000)
        word(save, actor + 0x5F, 30000)

        item_path = args.output / "ITEM.EXE"
        item = bytearray(item_path.read_bytes())
        header_size = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header_size:]
        record_index = 500 + 2
        record = int.from_bytes(
            image[record_index * 2:record_index * 2 + 2], "little")
        item[header_size + record + 0x16] = 0
        word(item, header_size + record + 0x26, 0)
        word(item, header_size + record + 0x2C, 60000)
        word(item, header_size + record + 0x2E, 0)
        word(item, header_size + record + 0x32, 0)
        word(item, header_size + record + 0x38, 1000)
        word(item, header_size + record + 0x3A, 0)
        word(item, header_size + record + 0x3E, 0)
        word(item, header_size + record + 0x42, 0)
        word(item, header_size + record + 0x44, 0)
        word(item, header_size + record + 0x46, 0)
        word(item, header_size + record + 0x4C, 0)

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(f"FIG player-basic SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(f"FIG player-basic ITEM differs: {item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG player-basic fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-basic fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
