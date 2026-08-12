#!/usr/bin/env python3
"""Stage traversal through AREA1's released hidden behavior-three entities."""

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
    "9c8ce6a6eb00ba4da6e8c915dd214d06c06b5c09d23c355f3e4d9fcecf504343"
RELEASE_BEHAVIORS = (3, 4, 3, 3, 3)
RELEASE_TARGET_FIELDS = (0, 0, 9270, 3, 10, 1, 65532, 4, 10, 40, 0)


def u16(data: bytes | bytearray | memoryview, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "little")


def set_word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


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

        mapz = (args.game / "MAPZ.DA1").read_bytes()
        header = u16(mapz, 8) * 16
        image = memoryview(mapz)[header:]
        location = u16(image, 8)
        area = u16(image, location + 26)
        count = u16(image, area + 4)
        behaviors = tuple(u16(image, area + 6 + (3 * count + index) * 2)
                          for index in range(count))
        target = tuple(u16(image, area + 6 + field * count * 2)
                       for field in range(11))
        if header != 512 or location != 968 or area != 17119 or count != 5 or \
                behaviors != RELEASE_BEHAVIORS or \
                target != RELEASE_TARGET_FIELDS:
            raise ValueError("release AREA1 behavior-three boundary changed")

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or u16(save, 0x408) != u16(image, area) or \
                u16(save, 0x40f) != 8 or u16(save, 0x424) != 8 or \
                u16(save, 0x012) != 38 or u16(save, 0x02a) != 80:
            raise ValueError("release AREA1 party/map boundary changed")
        # Entity zero and two other shipped behavior-three records overlap at
        # world cells (131..133,25).  Put the leader at (129,25).  One Right
        # step moves its centre to (130,25), whose new leading edge x=131
        # passes through those hidden records without collision or an event.
        set_word(save, 0x40d, 4906)
        set_word(save, 0x41b, 109)
        set_word(save, 0x41d, 13)
        actual_save = hashlib.sha256(save).hexdigest()
        if EXPECTED_SAVE_SHA256 and actual_save != EXPECTED_SAVE_SHA256:
            raise ValueError(f"behavior-three SAVE differs: {actual_save}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for name in ("MAPZ.DA1", "MAPZ.DAQ"):
            (args.output / name).write_bytes(mapz)
        for name in ("NAME1.DSK", "NAMEQ.DSK"):
            shutil.copy2(args.game / "NAME1.DSK", args.output / name)
        print(
            f"RPG AREA1 behavior-three fixture: SAVE {actual_save}, "
            f"MAPZ {hashlib.sha256(mapz).hexdigest()}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG AREA1 behavior-three fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
