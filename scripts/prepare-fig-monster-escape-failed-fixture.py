#!/usr/bin/env python3
"""Create the deterministic random-encounter escape-failure fixture.

The staged actor is level 60 but slower than both directory-64h monsters.
FIG's code-backed random cursor therefore makes the AI-type-two monster flee
first, then makes the remaining AI-type-one monster take the intimidation
failure branch.  The generated ORC copy is only for the DOSBox capture: all
eight hundredth-of-a-second choices point at the unmodified directory-64h
record.  Portable replay uses the source ORC and its fixed zero clock.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "bd37c895e149f3d6b3f3b5d762388fce3b486925af76c09d668f8dc4adeb8dec"
EXPECTED_SOURCE_ORC = "454912f918eaebe0e80cfdddb76a8f48daed9aab7e4d475acdb62e458518ccae"
EXPECTED_PINNED_ORC = "270dca68142b91a0d90215a27178338d27830b93fd2b7b88b18e94ad44a3c85d"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


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
        args.output.mkdir(parents=True)
        for name in ("MAPZ.DA1", "NAME1.DSK"):
            shutil.copy2(args.game / name, args.output / name)

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        word(save, 0x10, 1)       # one visible/commandable actor
        word(save, 0x4A0, 0)      # random-encounter selection path
        word(save, 0x408, 0x000A) # first FIG random-encounter region
        word(save, 0x41B, 100)
        word(save, 0x41D, 1)
        word(save, 0x49C, 0x1002) # sixth initiative draw -> failure roll one
        word(save, 0x49E, 20)
        word(save, 0x3F2, 1)
        for index in range(4):
            actor = 0x106 + index * 0x9F
            word(save, actor + 0x33, 1)
            word(save, actor + 0x5D, 0)
            word(save, actor + 0x5F, 0)
        actor = 0x106
        for offset, value in (
                (0x08, 0), (0x0C, 0), (0x0E, 1000),
                (0x2D, 1000), (0x2F, 1000), (0x31, 60),
                (0x33, 1), (0x41, 0), (0x43, 1000),
                (0x5D, 0), (0x5F, 0), (0x65, 0), (0x67, 0)):
            word(save, actor + offset, value)
        if sha256(save) != EXPECTED_SAVE:
            raise ValueError(
                f"FIG monster escape-failed SAVE differs: {sha256(save)}")
        (args.output / "SAVE.DA1").write_bytes(save)

        orc = bytearray((args.game / "ORC.EXE").read_bytes())
        if sha256(orc) != EXPECTED_SOURCE_ORC:
            raise ValueError(f"ORC.EXE differs: {sha256(orc)}")
        if len(orc) < 10 or orc[:2] != b"MZ":
            raise ValueError("ORC.EXE has no DOS MZ header")
        header_bytes = int.from_bytes(orc[8:10], "little") * 16
        record = bytes(orc[header_bytes + 100:header_bytes + 102])
        if len(record) != 2:
            raise ValueError("ORC.EXE directory entry 100 is truncated")
        for directory_offset in range(100, 116, 2):
            start = header_bytes + directory_offset
            orc[start:start + 2] = record
        if sha256(orc) != EXPECTED_PINNED_ORC:
            raise ValueError(
                f"FIG monster escape-failed pinned ORC differs: {sha256(orc)}")
        (args.output / "ORC.EXE").write_bytes(orc)
        print(
            "FIG monster escape-failed fixture: "
            f"SAVE={EXPECTED_SAVE}, pinned ORC={EXPECTED_PINNED_ORC}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG monster escape-failed fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
