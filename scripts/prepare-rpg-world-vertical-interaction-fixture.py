#!/usr/bin/env python3
"""Stage BUIN1's released entity for a vertical centre-ray interaction."""

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
    "5e3b74eb31b9cf9eaf395f6b8b5b9542737b29a3048adb6ef0b068b65b48adaa"
EXPECTED_MAPZ_SHA256 = \
    "c8b35df562edaa1929ba3e79b324e885b5b0893f5fa53447488d18f7cabdf433"
RELEASE_BEHAVIORS = (1, 7, 7, 4, 4, 7, 7, 7, 7, 7, 7)
RELEASE_TARGET_FIELDS = (5120, 6, 60878, 1, 10, 0, 65520, 4, 10, 40, 1)


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
        location = u16(image, 268)
        area = u16(image, location + 26)
        count = u16(image, area + 4)
        behavior_base = area + 6 + 3 * count * 2
        behaviors = tuple(u16(image, behavior_base + index * 2)
                          for index in range(count))
        target = tuple(u16(image, area + 6 + field * count * 2)
                       for field in range(11))
        if header != 512 or location != 4608 or area != 23549 or count != 11 or \
                behaviors != RELEASE_BEHAVIORS or \
                target != RELEASE_TARGET_FIELDS:
            raise ValueError("release BUIN1 interaction entity boundary changed")
        for index in range(1, count):
            set_word(image, behavior_base + index * 2, 3)
        actual_mapz = hashlib.sha256(mapz).hexdigest()
        if actual_mapz != EXPECTED_MAPZ_SHA256:
            raise ValueError(f"interaction MAPZ differs: {actual_mapz}")

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or u16(save, 0x40f) != 8 or \
                u16(save, 0x012) != 38 or u16(save, 0x02a) != 80:
            raise ValueError("release party/map boundary changed")
        # Entity zero spans world x=15..17 at y=169.  Put the leader at
        # (16,170), face north and issue Confirm: the first vertical 523d ray
        # must find it without any collision walk or automatic-event flag.
        for offset, value in (
                (0x408, 96), (0x40d, 55808), (0x40f, 8), (0x424, 268),
                (0x012, 30), (0x02a, 104), (0x0a2, 3),
                (0x41b, 0), (0x41d, 155)):
            set_word(save, offset, value)
        save[0x3f6:0x404] = image[location + 12:location + 26]
        set_dos_string(save, 0x42d, 22, r"E:\SWD2\T4\BUIN1.RAP")
        set_dos_string(save, 0x443, 22, r"E:\SWD2\T4\BUIN1.RAP")
        set_dos_string(save, 0x459, 22, r"E:\SWD2\RX\RI011.RIX")
        set_dos_string(save, 0x46f, 22, r"E:CHNA2.EXE")
        set_dos_string(save, 0x485, 24, r"E:CHNA2.DSK")
        actual_save = hashlib.sha256(save).hexdigest()
        if actual_save != EXPECTED_SAVE_SHA256:
            raise ValueError(f"interaction SAVE differs: {actual_save}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for name in ("MAPZ.DA1", "MAPZ.DAQ"):
            (args.output / name).write_bytes(mapz)
        for name in ("NAME1.DSK", "NAMEQ.DSK"):
            shutil.copy2(args.game / "NAME1.DSK", args.output / name)
        print(f"RPG BUIN1 vertical interaction fixture: SAVE {actual_save}, MAPZ {actual_mapz}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG BUIN1 vertical interaction fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
