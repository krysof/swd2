#!/usr/bin/env python3
"""Stage a live three-monster battle whose third slot installs B0 first."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "4c2061bcc2e69b19a0185a4ebb2f4491dae2c5dea09c0c831385db90ee54443a"
EXPECTED_ITEM = "ab3656c132ed8019102a06e06f38a45806b3d7076dc5a671e492d3a183b65b74"


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
        word(save, 0x10, 1)
        word(save, 0x4A0, 956)     # live definitions 402, 502 and 385
        word(save, 0x49C, 0x1024)
        word(save, 0x49E, 20)
        word(save, 0x3F2, 1)
        for index in range(4):
            actor = 0x106 + index * 0x9F
            for offset, value in (
                    (0x08, 0), (0x0C, 0), (0x0E, 10000),
                    (0x2D, 10000), (0x2F, 10000), (0x31, 1), (0x33, 1),
                    (0x41, 0), (0x43, 1000), (0x5D, 0), (0x5F, 0),
                    (0x65, 0), (0x67, 0)):
                word(save, actor + offset, value)
            for offset in (0x27, 0x28, 0x2A, 0x2B):
                save[actor + offset] = 0

        item_path = args.output / "ITEM.EXE"
        item = bytearray(item_path.read_bytes())
        header_size = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header_size:]

        def record(monster_id: int) -> int:
            index = monster_id + 2
            return int.from_bytes(
                image[index * 2:index * 2 + 2], "little")

        def set_fields(monster_id: int,
                       fields: tuple[tuple[int, int], ...]) -> None:
            base = header_size + record(monster_id)
            for offset, value in fields:
                word(item, base + offset, value)

        # All three shipped formation slots remain alive, so this is not the
        # original's pathological pre-dead-slot state.  Slot 2 has the high
        # speed and deterministically enters 23b1 before the player/other two.
        idle = (
            (0x2C, 60000), (0x2E, 0), (0x32, 0), (0x36, 1),
            (0x38, 0), (0x3E, 0), (0x40, 0), (0x42, 0),
            (0x44, 1000), (0x46, 0),
        )
        set_fields(402, idle)
        set_fields(502, idle)
        set_fields(385, (
            (0x2C, 60000), (0x2E, 9), (0x32, 118), (0x36, 1), (0x38, 0),
            (0x3E, 0), (0x40, 60000), (0x42, 0),
            (0x44, 1000), (0x46, 0),
        ))

        save_digest = sha256(save)
        item_digest = sha256(item)
        if save_digest != EXPECTED_SAVE:
            raise ValueError(
                f"FIG monster-third-slot-medium SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(
                f"FIG monster-third-slot-medium ITEM differs: {item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG monster-third-slot-medium fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG monster-third-slot-medium fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
