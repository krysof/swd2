#!/usr/bin/env python3
"""Stage a deterministic six-round FIG monster attack-buff expiry battle."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "361abb3e88934f31dba270362d60bcea3f608246c84a0bc004fca0df26c6e692"
EXPECTED_ITEM = "ef92e8055c9e1f3e09df856955640127aca2ba7ae97743f5443eed9ad94b7ab8"


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

        save_path = args.output / "SAVE.DA1"
        save = bytearray(save_path.read_bytes())
        actor = 0x106
        word(save, 0x10, 1)
        word(save, 0x49C, 0x1004)       # effect-67 duration draw is four
        word(save, 0x49E, 0xFFFF)
        word(save, 0x4A0, 392)
        word(save, actor + 0x08, 0)
        word(save, actor + 0x0C, 10000)
        word(save, actor + 0x0E, 60000)
        save[actor + 0x26] = 1          # survive the fallback physical turns
        word(save, actor + 0x2D, 60000)
        word(save, actor + 0x2F, 60000)
        word(save, actor + 0x31, 1)
        word(save, actor + 0x33, 60000)
        word(save, actor + 0x41, 10000)
        word(save, actor + 0x43, 60000)
        word(save, actor + 0x5D, 30000)
        word(save, actor + 0x5F, 30000)

        item_path = args.output / "ITEM.EXE"
        item = bytearray(item_path.read_bytes())
        header_size = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header_size:]
        record = int.from_bytes(image[502 * 2:502 * 2 + 2], "little")
        base = header_size + record
        item[base + 0x16] = 0
        word(item, base + 0x26, 0)
        word(item, base + 0x28, 0)
        word(item, base + 0x2C, 60000)
        word(item, base + 0x2E, 9)
        word(item, base + 0x32, 0)
        word(item, base + 0x38, 0)
        word(item, base + 0x3A, 0)
        word(item, base + 0x3E, 9)
        word(item, base + 0x42, 38)
        word(item, base + 0x44, 1000)
        word(item, base + 0x46, 38)
        word(item, base + 0x4C, 0)

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(
                f"FIG monster-buff-expiry SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(
                f"FIG monster-buff-expiry ITEM differs: {item_digest}")
        save_path.write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG monster-buff-expiry fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG monster-buff-expiry fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
