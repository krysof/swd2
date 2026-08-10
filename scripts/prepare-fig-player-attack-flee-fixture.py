#!/usr/bin/env python3
"""Stage a fixed FIG battle whose nonfatal player attack is followed by a monster flee."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "fe5462be2365a12a36e61dbc4470222682ca6294a64fa0082edd529b05783bf2"
EXPECTED_ITEM = "f6d0c842bd2021717830b84bf1ec3eacbe984d1b5bc3e52d96450855c9fa6045"
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
        word(item, header_size + record + 0x28, 2)
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
            raise ValueError(f"FIG player-flee SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(f"FIG player-flee ITEM differs: {item_digest}")
        if orc_digest != EXPECTED_ORC:
            raise ValueError(f"FIG player-flee ORC differs: {orc_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        orc_path.write_bytes(orc)
        print(
            "FIG player-flee fixture: "
            f"SAVE={save_digest} ITEM={item_digest} ORC={orc_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-flee fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
