#!/usr/bin/env python3
"""Stage AREA1's released behavior-four gate collision."""

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
    "ac97a2d797121e6f617a00b87439735e8faeaddbaf3fbefea6d52308b55380db"
RELEASE_BEHAVIORS = (3, 4, 3, 3, 3)
RELEASE_TARGET_FIELDS = (
    17408, 0, 40246, 4, 10, 65534, 65528, 4, 10, 300, 0)


def u16(data: bytes | bytearray | memoryview, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "little")


def set_word(data: bytearray | memoryview, offset: int, value: int) -> None:
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
        target = tuple(u16(image, area + 6 + (field * count + 1) * 2)
                       for field in range(11))
        if header != 512 or location != 968 or area != 17119 or count != 5 or \
                behaviors != RELEASE_BEHAVIORS or \
                target != RELEASE_TARGET_FIELDS:
            raise ValueError("release AREA1 behavior-four boundary changed")

        save = bytearray((args.game / "SAVE.DA1").read_bytes())
        if len(save) != 0x546 or u16(save, 0x408) != u16(image, area) or \
                u16(save, 0x40f) != 8 or u16(save, 0x424) != 8 or \
                u16(save, 0x012) != 38 or u16(save, 0x02a) != 80:
            raise ValueError("release AREA1 party/map boundary changed")
        # The behavior-four gate occupies world cells (139..141,111).  Keep
        # the released actor-screen position and place the viewport so the
        # leader is at world (137,111).  Right changes facing/animation, but
        # the new leading edge at x=139 collides with the gate.  Its flags are
        # 000ah, so walking into it cannot dispatch directory word 300.
        set_word(save, 0x40d, 35882)
        set_word(save, 0x41b, 117)
        set_word(save, 0x41d, 99)
        actual_save = hashlib.sha256(save).hexdigest()
        if EXPECTED_SAVE_SHA256 and actual_save != EXPECTED_SAVE_SHA256:
            raise ValueError(f"behavior-four SAVE differs: {actual_save}")

        for name in ("SAVE.DA1", "SAVE.DAQ"):
            (args.output / name).write_bytes(save)
        for name in ("MAPZ.DA1", "MAPZ.DAQ"):
            (args.output / name).write_bytes(mapz)
        for name in ("NAME1.DSK", "NAMEQ.DSK"):
            shutil.copy2(args.game / "NAME1.DSK", args.output / name)
        print(
            f"RPG AREA1 behavior-four fixture: SAVE {actual_save}, "
            f"MAPZ {hashlib.sha256(mapz).hexdigest()}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"RPG AREA1 behavior-four fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
