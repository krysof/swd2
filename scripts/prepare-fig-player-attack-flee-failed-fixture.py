#!/usr/bin/env python3
"""Stage a fixed FIG battle whose nonfatal player attack is followed by a failed monster flee."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "3de946a0bdea931598203111a9a892d226ac4aef20e078f5635519affe513abb"
EXPECTED_ITEM = "e3e7879a9a22dff28c9769dc9cb98a1c7ebf7d107663175ef93e72bd475f5c91"
EXPECTED_ORC = "e73536ab6ac6af0f8c1744c1a782c92cc044fddba508187ad15a3b030eff67bb"


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
        word(save, 0x4A0, 0)
        word(save, 0x408, 0x000A)
        word(save, 0x41B, 100)
        word(save, 0x41D, 1)
        word(save, 0x49C, 0x1004)
        word(save, 0x49E, 0xFFFF)
        word(save, actor + 0x0C, 30000)
        word(save, actor + 0x2D, 1000)
        word(save, actor + 0x2F, 1000)
        word(save, actor + 0x31, 100)
        word(save, actor + 0x33, 60)
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
        word(item, header_size + record + 0x28, 1)
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

        orc_path = args.output / "ORC.EXE"
        orc = bytearray(orc_path.read_bytes())
        orc_header_size = int.from_bytes(orc[8:10], "little") * 16
        image = memoryview(orc)[orc_header_size:]
        formation = bytes(image[392:394])
        for directory_offset in range(100, 116, 2):
            orc[orc_header_size + directory_offset:
                orc_header_size + directory_offset + 2] = formation

        save_digest = sha256(save)
        item_digest = sha256(item)
        orc_digest = sha256(orc)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(f"FIG player-flee-failed SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(f"FIG player-flee-failed ITEM differs: {item_digest}")
        if orc_digest != EXPECTED_ORC:
            raise ValueError(f"FIG player-flee-failed ORC differs: {orc_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        orc_path.write_bytes(orc)
        print(
            "FIG player-flee-failed fixture: "
            f"SAVE={save_digest} ITEM={item_digest} ORC={orc_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-flee-failed fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
