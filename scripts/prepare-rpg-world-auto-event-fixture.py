#!/usr/bin/env python3
"""Stage the released SBOUT automatic collision-event position."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


RELEASE_SHA256 = {
    "SAVE.DA1": "9ca3a8029f4d9d67dffff728d3f8e8902409f94954f10d7a9a3e0c90d053007e",
    "MAPZ.DA1": "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917",
    "NAME1.DSK": "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba",
}
EXPECTED_SAVE_SHA256 = \
    "d824f21396965c026ac691daa76de5099ddc314828a2a6b3d7f3d1a46066eab4"


def set_word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def set_dos_string(data: bytearray, offset: int, capacity: int,
                   value: str) -> None:
    encoded = value.encode("ascii")
    if len(encoded) >= capacity:
        raise ValueError(f"DOS string does not fit at {offset:#x}")
    data[offset:offset + capacity] = encoded + bytes(capacity - len(encoded))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        for name, expected in RELEASE_SHA256.items():
            actual = hashlib.sha256((args.game / name).read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError(f"{name} differs from the release: {actual}")
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or \
                int.from_bytes(save[0x40F:0x411], "little") != 8 or \
                int.from_bytes(save[0x012:0x014], "little") != 38 or \
                int.from_bytes(save[0x02A:0x02C], "little") != 80:
            raise ValueError("release party/map boundary changed")

        # MAPZ directory byte offset 12 is SBOUT.  Entity five occupies the
        # three words beginning at cell pointer 25008 and carries flags 800ah
        # plus CHNA1 directory offset 220.  Centre the leader at world (79,69):
        # a single east poll is blocked by that entity and must enter the
        # automatic-event path without a Confirm input.
        set_word(save, 0x408, 12)       # area flags
        set_word(save, 0x40D, 20646)    # viewport RAP pointer
        set_word(save, 0x40F, 8)        # RAP image base
        set_word(save, 0x424, 12)       # MAPZ directory byte offset
        set_word(save, 0x012, 38)       # leader screen x
        set_word(save, 0x02A, 80)       # leader screen y
        set_word(save, 0x0A2, 0)        # leader initially faces south
        set_word(save, 0x41B, 59)       # viewport x
        set_word(save, 0x41D, 57)       # viewport y
        save[0x3F6:0x404] = bytes.fromhex(
            "b3a5a140a140bab5a140a140b9eb")
        set_dos_string(save, 0x42D, 22, r"E:\SWD2\T1\SBOUT.RSK")
        set_dos_string(save, 0x443, 22, r"E:\SWD2\T1\SBOUT.RSK")
        set_dos_string(save, 0x459, 22, r"E:\SWD2\RX\CA01.RIX")
        set_dos_string(save, 0x46F, 22, r"E:CHNA1.EXE")
        set_dos_string(save, 0x485, 24, r"E:CHNA1.DSK")
        actual = hashlib.sha256(save).hexdigest()
        if actual != EXPECTED_SAVE_SHA256:
            raise ValueError(f"automatic-event SAVE differs: {actual}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for source, destinations in (
                ("MAPZ.DA1", ("MAPZ.DA1", "MAPZ.DAQ")),
                ("NAME1.DSK", ("NAME1.DSK", "NAMEQ.DSK"))):
            for destination in destinations:
                shutil.copy2(args.game / source, args.output / destination)
        print(f"RPG SBOUT automatic-event fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG SBOUT automatic-event fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
