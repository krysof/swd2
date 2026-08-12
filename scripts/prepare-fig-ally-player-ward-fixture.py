#!/usr/bin/env python3
"""Stage captured item 399 so its shipped effect-69h special acts first."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "54c9a266129fb8be22b5541967e8c84bcc11ff2c2e2922ac7aecb6887aa3407f"
EXPECTED_ITEM = "50e151c00f18385e7e2ccbd709ee206bdbf70459344f25c8a17109c4eb23e9b0"


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        actor = 0x106
        word(save, 0x10, 1)
        word(save, 0x4A0, 392)
        word(save, 0x49C, 0x1002)
        word(save, 0x382, 399)
        for offset, value in (
                (0x0C, 1), (0x0E, 60000), (0x2D, 60000),
                (0x2F, 60000), (0x31, 1), (0x33, 60000),
                (0x35, 1000), (0x37, 1000), (0x41, 1),
                (0x43, 60000), (0x55, 1000), (0x57, 1000),
                (0x5D, 60000), (0x5F, 60000), (0x65, 0), (0x67, 0)):
            word(save, actor + offset, value)

        item = bytearray((args.output / "ITEM.EXE").read_bytes())
        header = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header:]
        directory_bytes = int.from_bytes(image[2:4], "little")
        if directory_bytes < 4 or directory_bytes % 2:
            raise ValueError("ITEM.EXE directory is malformed")
        record = int.from_bytes(
            image[(399 + 2) * 2:(399 + 3) * 2], "little")
        # Preserve the shipped ability 37/effect 69h descriptor. Only force
        # item 399's AI selection and initiative so this isolated capture
        # reaches that already-shipped special before the following monster.
        for offset, value in (
                (0x2E, 9), (0x32, 0), (0x3A, 0), (0x3E, 9),
                (0x40, 60000), (0x42, 37), (0x44, 60000), (0x46, 0)):
            word(item, header + record + offset, value)

        save_digest = hashlib.sha256(save).hexdigest()
        item_digest = hashlib.sha256(item).hexdigest()
        if save_digest != EXPECTED_SAVE or item_digest != EXPECTED_ITEM:
            raise ValueError(
                "FIG captured-ally ward fixture differs: "
                f"SAVE={save_digest} ITEM={item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        (args.output / "ITEM.EXE").write_bytes(item)
        print(
            "FIG captured-ally ward fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG captured-ally ward fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
