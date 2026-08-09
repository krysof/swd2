#!/usr/bin/env python3
"""Create the deterministic release-format save used by the travel replay."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


RELEASE_SHA256 = {
    "SAVE.DAQ": "ba9a6fef7ff12697cfd72e3197125a18995fd78388f766b18f508f3f08f49099",
    "MAPZ.DAQ": "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917",
    "NAMEQ.DSK": "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba",
}
UNLOCKED = (0, 2, 3, 5, 7, 8, 11, 13, 17, 20, 27, 33)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    for name, expected in RELEASE_SHA256.items():
        actual = digest(args.game / name)
        if actual != expected:
            parser.error(f"{name} is not the supported release file: {actual}")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    for source, target in (
            ("SAVE.DAQ", "SAVE.DA1"),
            ("MAPZ.DAQ", "MAPZ.DA1"),
            ("NAMEQ.DSK", "NAME1.DSK")):
        shutil.copy2(args.game / source, args.output / target)

    save_path = args.output / "SAVE.DA1"
    save = bytearray(save_path.read_bytes())
    if len(save) != 0x546:
        parser.error("release SAVE.DAQ size changed")
    actor = 0x106
    save[actor + 0x6d] = 99       # 乘龍念法 / field action 29h
    save[actor + 0x55:actor + 0x57] = (100).to_bytes(2, "little")
    flags = int.from_bytes(save[0x408:0x40a], "little") | 0x8000
    save[0x408:0x40a] = flags.to_bytes(2, "little")
    save[0x51e:0x51e + 36] = bytes(36)
    for index in UNLOCKED:
        save[0x51e + index] = 1
    # The release has 34 named entries, one reserved zero, then its sentinel.
    save[0x51e + 34] = 0
    save[0x51e + 35] = 0x0f
    save_path.write_bytes(save)
    print(
        "RPG travel fixture: SAVE.DAQ -> slot 1, action 29h enabled, "
        "12 release-format destinations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
