#!/usr/bin/env python3
"""Stage FIG player attack-buff expiry after a failed capture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "b19b4542bbf287c4d4556845b46c76c452c7eb66384e7386f614ef9878998658"
EXPECTED_SUCCESS_SAVE = "5146ae0564d03e3755a9a462edb14acac6852b45aa2d51b9316eeb839e07a843"
EXPECTED_MAPZ = "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917"
EXPECTED_NAME = "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba"
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
        "--success", action="store_true",
        help="stage level 100 so the expiry-turn capture succeeds")
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save_path = args.output / "SAVE.DA1"
        save = bytearray(save_path.read_bytes())
        actor = 0x106
        word(save, 0x10, 1)             # one active party member
        word(save, 0x408, 0)            # random directory base 100
        word(save, 0x3F0, 0)            # expose actor-zero capture command
        word(save, 0x3F2, 1)
        word(save, 0x49C, 0x1020)       # minimum attack-buff duration draw
        word(save, 0x49E, 20)           # stable physical/damage draws
        word(save, 0x4A0, 0)            # preserve random-encounter rules

        # Ability 38 applies effect 67 (力量加強). Two deliberately
        # non-lethal attacks leave its counter at one. Actor level one then
        # makes the random-encounter capture fail on the expiry turn.
        for offset, value in (
                (0x0C, 1), (0x0E, 60000),
                (0x2D, 60000), (0x2F, 60000),
                (0x31, 100 if args.success else 1),
                (0x33, 60000), (0x35, 60000), (0x37, 60000),
                (0x41, 1), (0x43, 60000),
                (0x55, 60000), (0x57, 60000),
                (0x5D, 60000), (0x5F, 60000),
                (0x6D, 38), (0x6E, 1)):
            word(save, actor + offset, value)
        for slot in range(50):
            word(save, 0x382 + slot * 2, 0)

        # DOS hundredths choose one of directory words 100..114. Redirect all
        # eight to the shipped one-monster record at directory 288. Retain
        # every displaced boundary through eight originally empty words.
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
        mapz_digest = sha256((args.output / "MAPZ.DA1").read_bytes())
        name_digest = sha256((args.output / "NAME1.DSK").read_bytes())
        orc_digest = sha256(orc)
        expected_save = EXPECTED_SUCCESS_SAVE if args.success else EXPECTED_SAVE
        if save_digest != expected_save:
            raise ValueError(
                f"FIG player-buff-expiry-capture SAVE differs: {save_digest}")
        if mapz_digest != EXPECTED_MAPZ or name_digest != EXPECTED_NAME:
            raise ValueError(
                "FIG player-buff-expiry-capture companion save files differ")
        if orc_digest != EXPECTED_ORC:
            raise ValueError(
                f"FIG player-buff-expiry-capture ORC differs: {orc_digest}")
        save_path.write_bytes(save)
        orc_path.write_bytes(orc)
        print(
            "FIG player-buff-expiry-capture fixture: "
            f"SAVE={save_digest} ORC={orc_digest} MAPZ={mapz_digest} "
            f"NAME={name_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG player-buff-expiry-capture fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
