#!/usr/bin/env python3
"""Stage a deterministic original 2731 mediator-dismissal battle."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "4c2061bcc2e69b19a0185a4ebb2f4491dae2c5dea09c0c831385db90ee54443a"
EXPECTED_ITEM = "00d9d8d8c38124bad51016535d42d762f296cce627e0bb9f4d387d52751af2c9"


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
        word(save, 0x4A0, 956)     # definitions 402, 502 and 385
        word(save, 0x49C, 0x1024)  # summon medium 0, then dismiss it next round
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
        save_digest = sha256(save)

        item_path = args.output / "ITEM.EXE"
        item = bytearray(item_path.read_bytes())
        header_size = int.from_bytes(item[8:10], "little") * 16
        image = memoryview(item)[header_size:]
        def record(monster_id: int) -> int:
            index = monster_id + 2
            return int.from_bytes(
                image[index * 2:index * 2 + 2], "little")

        # Keep the shipped directory-956 formation and untouched FIG.EXE, but
        # make two of its ordinary monsters form a deterministic same-round
        # pair. Definition 402 acts first and uses shipped ability 54 to create
        # medium zero; definition 502 then selects shipped special 53 through
        # either A/B branch and enters original 2731 to remove it. This avoids
        # relying on AUTOTYPE timing between two complete rounds.
        summoner = record(402)
        for offset, value in (
                (0x2C, 1000), (0x2E, 9), (0x32, 54), (0x36, 1),
                (0x3E, 0), (0x40, 30000), (0x42, 0),
                (0x44, 1000), (0x46, 0)):
            word(item, header_size + summoner + offset, value)
        dismisser = record(502)
        for offset, value in (
                (0x2E, 9), (0x32, 0), (0x36, 1), (0x3E, 9),
                (0x40, 20000), (0x42, 53), (0x44, 1000), (0x46, 53)):
            word(item, header_size + dismisser + offset, value)
        # The third shipped slot must not add another action to the checkpoint.
        word(item, header_size + record(385) + 0x2C, 0)
        item_digest = sha256(item)

        if save_digest != EXPECTED_SAVE:
            raise ValueError(f"FIG medium-dismiss SAVE differs: {save_digest}")
        if item_digest != EXPECTED_ITEM:
            raise ValueError(f"FIG medium-dismiss ITEM differs: {item_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        item_path.write_bytes(item)
        print(
            "FIG medium-dismiss fixture: "
            f"SAVE={save_digest} ITEM={item_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG medium-dismiss fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
