#!/usr/bin/env python3
"""Stage a fixed FIG battle whose first critical attack is nonfatal."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "9f195327bab34027bee37e972619f05861d2824fca7194452a5980fc91a9105a"
EXPECTED_ITEM = "ecdb54e554bfdcfadc5c1979b3e5000a4ae69789a3780f7db3747928c9a249fd"


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
        word(save, 0x49E, 1)
        word(save, actor + 0x0C, 1000)
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
        word(item, header_size + record + 0x2C, 60000)
        word(item, header_size + record + 0x4C, 0)

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(
                f"FIG player-critical-hit SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(
                f"FIG player-critical-hit ITEM differs: {item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG player-critical-hit fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-critical-hit fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
