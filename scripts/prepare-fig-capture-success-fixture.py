#!/usr/bin/env python3
"""Stage a deterministic original random-encounter capture success."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "7a6aa873909e024a30b704345213cccec2a28dce6c476c3c799ca8dfbc1342c3"
EXPECTED_FAILURE_SAVE = "5b6a60430d2d9c12d4924f9cc9fc93598ee8e55447c7a68d6cdffaca3f6056e5"
EXPECTED_ESCAPE_FAILURE_SAVE = \
    "3d475f7e65f187e1820f249ac8a95f333c1e6c95e86dea8538bcc039a7812dbb"
EXPECTED_ORC = "e9dd243bc22afcd93f5474de6f481651fc7e45ead2aaeba8e06f78e9b9befec5"


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--failure", action="store_true",
        help="stage level 1 so the same capture attempt reaches 0e42")
    mode.add_argument(
        "--escape-failure", action="store_true",
        help="stage a durable level-1 actor and random cursor 0x1002")
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save = bytearray((args.output / "SAVE.DA1").read_bytes())
        word(save, 0x10, 1)
        word(save, 0x4A0, 0)       # preserve random-encounter capture rules
        word(save, 0x408, 0)       # no region row: random directory base 100
        word(save, 0x3F0, 0)       # expose actor-zero capture command
        word(save, 0x3F2, 1)
        actor = 0x106
        for offset, value in (
                (0x08, 0), (0x0C, 1000), (0x0E, 1000),
                (0x2D, 1000), (0x2F, 1000),
                (0x31, 1 if args.failure or args.escape_failure else 100),
                (0x33, 1),
                (0x41, 1000), (0x43, 1000),
                (0x5D, 30000), (0x5F, 30000), (0x65, 0), (0x67, 0)):
            word(save, actor + offset, value)
        if args.escape_failure:
            # 09b8 consumes the same FIG random stream as initiative. Cursor
            # 1002 produces a failed player escape after actor zero acts first.
            # Large HP/defence values keep the following enemy turn stable.
            for offset in (0x0E, 0x2D, 0x2F, 0x43):
                word(save, actor + offset, 30000)
            word(save, 0x49C, 0x1002)
        for slot in range(50):
            word(save, 0x382 + slot * 2, 0)

        # DOS hundredths choose one of directory words 100..114. Redirect all
        # eight to the shipped one-monster record from directory 288, while
        # retaining every displaced record through eight deliberately empty
        # directory words so the ORC record boundary set remains unchanged.
        orc_path = args.output / "ORC.EXE"
        orc = bytearray(orc_path.read_bytes())
        header_size = int.from_bytes(orc[8:10], "little") * 16
        directory_bytes = 1252
        target = int.from_bytes(
            orc[header_size + 288:header_size + 290], "little")
        empty = [
            offset for offset in range(0, directory_bytes, 2)
            if int.from_bytes(
                orc[header_size + offset:header_size + offset + 2],
                "little") == 0
        ]
        if len(empty) < 8:
            raise ValueError("ORC lacks eight empty directory words")
        for source, keep in zip(range(100, 116, 2), empty[:8]):
            displaced = int.from_bytes(
                orc[header_size + source:header_size + source + 2],
                "little")
            word(orc, header_size + keep, displaced)
            word(orc, header_size + source, target)

        save_digest = sha256(save)
        orc_digest = sha256(orc)
        if args.escape_failure:
            expected_save = EXPECTED_ESCAPE_FAILURE_SAVE
            fixture_kind = "player-escape-failure"
        elif args.failure:
            expected_save = EXPECTED_FAILURE_SAVE
            fixture_kind = "capture-failure"
        else:
            expected_save = EXPECTED_SAVE
            fixture_kind = "capture-success"
        if save_digest != expected_save:
            raise ValueError(f"FIG capture-success SAVE differs: {save_digest}")
        if orc_digest != EXPECTED_ORC:
            raise ValueError(f"FIG capture-success ORC differs: {orc_digest}")
        (args.output / "SAVE.DA1").write_bytes(save)
        orc_path.write_bytes(orc)
        print(
            f"FIG {fixture_kind} fixture: "
            f"SAVE={save_digest} ORC={orc_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG capture-success fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
