#!/usr/bin/env python3
"""Stage a fixed FIG battle whose nonfatal player attack is followed by resisted monster special ability 117."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "b00b10373db625eab3a831e27349d909c4d14a2540be11d0fe1e5230af8d1f8a"
EXPECTED_ITEM = "ce3cb0d93fd04f27435230b5140ab6159e45b277a0c906663e14e4222c4c57d8"


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
        word(save, 0x49C, 0x1000)
        word(save, 0x49E, 0xFFFF)
        word(save, actor + 0x0C, 30000)
        word(save, actor + 0x2D, 1000)
        word(save, actor + 0x2F, 1000)
        word(save, actor + 0x31, 100)
        word(save, actor + 0x33, 1)
        word(save, actor + 0x41, 30000)
        word(save, actor + 0x5D, 30000)
        word(save, actor + 0x5F, 30000)
        for offset in (0x27, 0x28, 0x2A, 0x2B):
            save[actor + offset] = 0
        save[actor + 0x2B] = 1

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
        word(item, header_size + record + 0x28, 0)
        word(item, header_size + record + 0x2E, 9)
        word(item, header_size + record + 0x32, 0)
        word(item, header_size + record + 0x38, 1000)
        word(item, header_size + record + 0x3A, 0)
        word(item, header_size + record + 0x3E, 9)
        word(item, header_size + record + 0x42, 117)
        word(item, header_size + record + 0x44, 1000)
        word(item, header_size + record + 0x46, 117)
        word(item, header_size + record + 0x4C, 0)

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(f"FIG player-attack-special-resisted SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(f"FIG player-attack-special-resisted ITEM differs: {item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG player-attack-special-resisted fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-attack-special-resisted fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
