#!/usr/bin/env python3
"""Stage captured item 376 with a deterministic special-B B0 dismissal."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "eaec625878e5ed8dafeed356b10f5f6056163d5e80ca475506b4b502bda419f9"
EXPECTED_ITEM = "88a20040a1c4645f1207f0d4c16272d2e8d46edd07934a758355d1849e9bdbb7"


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
        word(save, 0x4A0, 392)
        word(save, 0x49C, 0x12F2)
        word(save, 0x382, 376)
        for offset in range(0x384, 0x3E6, 2):
            word(save, offset, 0)
        for offset, value in (
                (0x0C, 1), (0x0E, 60000), (0x2D, 60000),
                (0x2F, 60000), (0x31, 1), (0x33, 60000),
                (0x35, 1000), (0x37, 1000), (0x41, 1),
                (0x43, 60000), (0x55, 1000), (0x57, 1000),
                (0x5D, 60000), (0x5F, 60000), (0x65, 0), (0x67, 0)):
            word(save, actor + offset, value)
        save[actor + 0x6D:actor + 0x6D + 50] = bytes(50)

        item_path = args.output / "ITEM.EXE"
        item = bytearray(item_path.read_bytes())
        header_size = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header_size:]
        record_index = 376 + 2
        record = int.from_bytes(
            image[record_index * 2:record_index * 2 + 2], "little")
        # The shipped record has a zero primary chance.  Pin both chance
        # fields to nine and clear A/generic so every successful choice enters
        # special-B ability 83/effect 3fh without changing that effect record.
        for offset, value in (
                (0x2E, 9), (0x32, 0), (0x3E, 9),
                (0x42, 0), (0x46, 83)):
            word(item, header_size + record + offset, value)

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(
                f"FIG captured-ally empty-B0 SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(
                f"FIG captured-ally empty-B0 ITEM differs: {item_digest}")
        save_path.write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG captured-ally empty-B0 fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG captured-ally empty-B0 fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
