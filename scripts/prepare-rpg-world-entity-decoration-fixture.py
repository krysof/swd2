#!/usr/bin/env python3
"""Stage SWRO7's released behavior-five animated decoration in isolation."""

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
    "81e736432afcbc5c2a8886fb8dfd3101592a713cc3694049cfa2f3ddad16ffa0"
EXPECTED_MAPZ_SHA256 = \
    "2ea750fd18539652e1f85b1bb24e70b425cd8451fdd3afbd6dedf862f3066712"
RELEASE_BEHAVIORS = (5, 0, 0)
RELEASE_TARGET_FIELDS = (3328, 0, 1510, 5, 10, 65535, 65529, 4, 10, 58, 0)


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
        location = u16(image, 46)
        area = u16(image, location + 26)
        count = u16(image, area + 4)
        behavior_base = area + 6 + 3 * count * 2
        behaviors = tuple(u16(image, behavior_base + index * 2)
                          for index in range(count))
        target = tuple(u16(image, area + 6 + field * count * 2)
                       for field in range(11))
        if header != 512 or location != 1500 or area != 19233 or count != 3 or \
                behaviors != RELEASE_BEHAVIORS or target != RELEASE_TARGET_FIELDS:
            raise ValueError("release SWRO7 animated-decoration boundary changed")
        for index in range(1, count):
            set_word(image, behavior_base + index * 2, 3)
        actual_mapz = hashlib.sha256(mapz).hexdigest()

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or u16(save, 0x40f) != 8 or \
                u16(save, 0x012) != 38 or u16(save, 0x02a) != 80:
            raise ValueError("release party/map boundary changed")
        # The decoration occupies world (47,11).  A viewport at (24,1)
        # keeps its complete animation near the centre while the leader stays
        # well below and to the left at world (30,22).
        for offset, value in (
                (0x408, u16(image, area)), (0x40d, 184), (0x40f, 8),
                (0x413, 40), (0x415, 25), (0x417, 64), (0x419, 38),
                (0x424, 46), (0x012, 10), (0x02a, 152), (0x0a2, 0),
                (0x41b, 24), (0x41d, 1)):
            set_word(save, offset, value)
        save[0x3f6:0x404] = image[location + 12:location + 26]
        set_dos_string(save, 0x42d, 22, r"E:\SWD2\T1\SWRO7.RS4")
        set_dos_string(save, 0x443, 22, r"E:\SWD2\T1\SWRO7.RRO")
        set_dos_string(save, 0x459, 22, r"E:\SWD2\RX\TO01.RIX")
        set_dos_string(save, 0x46f, 22, r"E:CHNA1.EXE")
        set_dos_string(save, 0x485, 24, r"E:CHNA1.DSK")
        actual_save = hashlib.sha256(save).hexdigest()
        if actual_save != EXPECTED_SAVE_SHA256 or \
                actual_mapz != EXPECTED_MAPZ_SHA256:
            raise ValueError(
                f"fixture digests differ: SAVE {actual_save}, MAPZ {actual_mapz}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for name in ("MAPZ.DA1", "MAPZ.DAQ"):
            (args.output / name).write_bytes(mapz)
        for name in ("NAME1.DSK", "NAMEQ.DSK"):
            shutil.copy2(args.game / "NAME1.DSK", args.output / name)
        print(
            f"RPG SWRO7 animated-decoration fixture: SAVE {actual_save}, "
            f"MAPZ {actual_mapz}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG SWRO7 animated-decoration fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
