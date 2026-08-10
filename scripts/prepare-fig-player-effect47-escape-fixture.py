#!/usr/bin/env python3
"""Stage a deterministic random battle for ability 7 / effect 47h escape."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "f0c704b15e79441619fecbd629aa9fc7e07a935aee696ba8182eae60ecf1f564"
EXPECTED_ITEM_SAVE = "5cde7abfd7106bd2611e708349bd2ac0a0ad40ecb801f0fd19608a128ab90937"
EXPECTED_ORC = "e9dd243bc22afcd93f5474de6f481651fc7e45ead2aaeba8e06f78e9b9befec5"


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--item", action="store_true",
        help="stage type-10 item 237 instead of learned ability 7")
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save = bytearray((args.output / "SAVE.DA1").read_bytes())
        word(save, 0x10, 1)
        word(save, 0x4A0, 0)       # random-encounter rules permit selector 47h
        word(save, 0x408, 0)       # random directory base 100
        actor = 0x106
        for offset, value in (
                (0x08, 0), (0x0C, 1000), (0x0E, 1000),
                (0x2D, 1000), (0x2F, 1000),
                (0x35, 1000), (0x37, 1000),
                (0x41, 1000), (0x43, 1000),
                (0x55, 1000), (0x57, 1000),
                (0x5D, 30000), (0x5F, 30000)):
            word(save, actor + offset, value)
        save[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
        if not args.item:
            save[actor + 0x6D] = 7  # 逃遁術, selector 47h
        for slot in range(50):
            word(save, 0x382 + slot * 2, 0)
        if args.item:
            word(save, 0x382, 237)  # type-10 selector 47h wrapper

        # Redirect all eight hundredths-selected random directory words to the
        # shipped one-monster formation at directory offset 288. Preserve the
        # displaced offsets in otherwise empty directory words, so no ORC
        # record boundary is invented or removed.
        orc_path = args.output / "ORC.EXE"
        orc = bytearray(orc_path.read_bytes())
        header_size = int.from_bytes(orc[8:10], "little") * 16
        target = int.from_bytes(
            orc[header_size + 288:header_size + 290], "little")
        empty = [
            offset for offset in range(0, 1252, 2)
            if int.from_bytes(
                orc[header_size + offset:header_size + offset + 2],
                "little") == 0
        ]
        if len(empty) < 8:
            raise ValueError("ORC lacks eight empty directory words")
        for source, keep in zip(range(100, 116, 2), empty[:8]):
            displaced = int.from_bytes(
                orc[header_size + source:header_size + source + 2], "little")
            word(orc, header_size + keep, displaced)
            word(orc, header_size + source, target)

        save_digest = sha256(save)
        orc_digest = sha256(orc)
        expected_save = EXPECTED_ITEM_SAVE if args.item else EXPECTED_SAVE
        if save_digest != expected_save:
            raise ValueError(f"FIG player-effect-47 SAVE differs: {save_digest}")
        if orc_digest != EXPECTED_ORC:
            raise ValueError(f"FIG player-effect-47 ORC differs: {orc_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        orc_path.write_bytes(orc)
        print(
            f"FIG player-effect-47 escape fixture: "
            f"SAVE={save_digest} ORC={orc_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG player-effect-47 escape fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
