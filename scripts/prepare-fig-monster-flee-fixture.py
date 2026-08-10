#!/usr/bin/env python3
"""Create the deterministic random-encounter monster-flee fixture.

The original chooses one of eight adjacent ORC directory entries from the DOS
hundredth-of-a-second field.  For a reproducible DOSBox capture, the generated
ORC copy points all eight choices at directory entry 100's record.  FIG.EXE is
left untouched; the portable replay uses the unmodified ORC and its fixed zero
clock, which selects that same entry directly.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "ecba11f1625862dd877a5252609615fd5ceb896f12be159d501676b06ed63756"
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
        actor = 0x106
        for offset, value in (
                (0x08, 0), (0x0C, 1000), (0x0E, 1000),
                (0x2D, 1000), (0x2F, 1000), (0x31, 60),
                (0x33, 1), (0x41, 100), (0x43, 100),
                (0x5D, 100), (0x5F, 100), (0x65, 0), (0x67, 0)):
            word(save, actor + offset, value)
        if sha256(save) != EXPECTED_SAVE:
            raise ValueError(f"FIG monster-flee SAVE differs: {sha256(save)}")
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
                f"FIG monster-flee pinned ORC differs: {sha256(orc)}")
        (args.output / "ORC.EXE").write_bytes(orc)
        print(
            "FIG monster-flee fixture: "
            f"SAVE={EXPECTED_SAVE}, pinned ORC={EXPECTED_PINNED_ORC}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG monster-flee fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
