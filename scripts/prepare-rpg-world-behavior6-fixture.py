#!/usr/bin/env python3
"""Stage MA-DE behavior-six collision with unrelated entities hidden."""

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
    "2f843a2876f0abe877b809f89db0eec0aee58d314f440a136bd2e42b4ccf1567"
EXPECTED_MAPZ_SHA256 = \
    "ad1f35f2752784e7cf81604f27cbf9fffdcdeac7b7a72524974d5c1be2bf2bbc"
RELEASE_BEHAVIORS = (1, 3, 0, 2, 2, 6, 6, 3, 6, 7, 7, 7, 7)


def u16(data: bytes | bytearray | memoryview, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "little")


def set_word(data: bytearray | memoryview, offset: int, value: int) -> None:
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

        mapz = bytearray((args.game / "MAPZ.DA1").read_bytes())
        header = u16(mapz, 8) * 16
        image = memoryview(mapz)[header:]
        location = u16(image, 10)
        area = u16(image, location + 26)
        count = u16(image, area + 4)
        behavior_base = area + 6 + 3 * count * 2
        behaviors = tuple(u16(image, behavior_base + index * 2)
                          for index in range(count))
        if header != 512 or location != 996 or area != 17245 or count != 13 or \
                behaviors != RELEASE_BEHAVIORS:
            raise ValueError("release MA-DE entity boundary changed")
        # Keep entity five byte-for-byte intact.  Hide the other twelve
        # entities only in the capture fixture so their autonomous movement
        # cannot obscure the target's single-frame disappearance.
        for index in range(count):
            if index != 5:
                set_word(image, behavior_base + index * 2, 3)
        actual_mapz = hashlib.sha256(mapz).hexdigest()
        if actual_mapz != EXPECTED_MAPZ_SHA256:
            raise ValueError(f"behavior-six MAPZ differs: {actual_mapz}")

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or \
                int.from_bytes(save[0x40F:0x411], "little") != 8 or \
                int.from_bytes(save[0x012:0x014], "little") != 38 or \
                int.from_bytes(save[0x02A:0x02C], "little") != 80:
            raise ValueError("release party/map boundary changed")
        # Entity five begins at MA-DE cell (107,142).  World (106,142) places
        # its three-word footprint on the leader's east edge.  Right is
        # blocked, behavior six changes to hidden behavior three, and no CHNA
        # event is entered despite the record's directory word 20.
        set_word(save, 0x408, 0x2010)
        set_word(save, 0x40D, 46980)
        set_word(save, 0x40F, 8)
        set_word(save, 0x424, 10)
        set_word(save, 0x012, 38)
        set_word(save, 0x02A, 80)
        set_word(save, 0x0A2, 0)
        set_word(save, 0x41B, 86)
        set_word(save, 0x41D, 130)
        save[0x3F6:0x404] = bytes.fromhex(
            "a677a140a140a977a140a140a7f8")
        set_dos_string(save, 0x42D, 22, r"E:\SWD2\T1\MA-DE.RSK")
        set_dos_string(save, 0x443, 22, r"E:\SWD2\T1\MA-DE.RSK")
        set_dos_string(save, 0x459, 22, r"E:\SWD2\RX\TO01.RIX")
        set_dos_string(save, 0x46F, 22, r"E:CHNA1.EXE")
        set_dos_string(save, 0x485, 24, r"E:CHNA1.DSK")
        actual_save = hashlib.sha256(save).hexdigest()
        if actual_save != EXPECTED_SAVE_SHA256:
            raise ValueError(f"behavior-six SAVE differs: {actual_save}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for name in ("MAPZ.DA1", "MAPZ.DAQ"):
            (args.output / name).write_bytes(mapz)
        for name in ("NAME1.DSK", "NAMEQ.DSK"):
            shutil.copy2(args.game / "NAME1.DSK", args.output / name)
        print(
            f"RPG MA-DE behavior-six fixture: SAVE {actual_save}, "
            f"MAPZ {actual_mapz}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG MA-DE behavior-six fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
