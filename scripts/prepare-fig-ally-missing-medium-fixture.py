#!/usr/bin/env python3
"""Stage the shipped captured-ally ability 116 missing-B0 route."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "290c0c64a8e3931eb6fba27dfc82c8e1fe72e2668b0d71aed1af15c9539f04ee"
EXPECTED_ITEM = "29dac522a2b90bbc5eb29135596f882923b69d4443cf03c96b4a3d24e1121b2b"


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
        word(save, 0x49C, 0x1026)
        word(save, 0x382, 402)
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
            image[(402 + 2) * 2:(402 + 3) * 2], "little")
        # Item 402 already names generic ability 116/effect 48h.  Only make
        # the ally act early with enough AP so the original 1048 route reaches
        # its shipped, deliberately unflagged B0 prerequisite deterministically.
        for offset, value in (
                (0x2E, 9), (0x32, 116), (0x3A, 0), (0x3E, 0),
                (0x40, 60000), (0x42, 0), (0x44, 60000), (0x46, 0)):
            word(item, header + record + offset, value)

        save_digest = hashlib.sha256(save).hexdigest()
        item_digest = hashlib.sha256(item).hexdigest()
        if save_digest != EXPECTED_SAVE or item_digest != EXPECTED_ITEM:
            raise ValueError(
                "FIG captured-ally missing-medium fixture differs: "
                f"SAVE={save_digest} ITEM={item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        (args.output / "ITEM.EXE").write_bytes(item)
        print(
            "FIG captured-ally missing-medium fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG captured-ally missing-medium fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
