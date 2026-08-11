#!/usr/bin/env python3
"""Stage FIG player attack-buff expiry after a direct item."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "32c63d8fee0304b1f81899a0c402d7d693d69a7a44f0d73c80433d457f36755c"
EXPECTED_MAPZ = "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917"
EXPECTED_NAME = "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba"


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save_path = args.output / "SAVE.DA1"
        save = bytearray(save_path.read_bytes())
        actor = 0x106
        word(save, 0x10, 1)             # one active party member
        word(save, 0x49C, 0x1020)       # minimum attack-buff duration draw
        word(save, 0x49E, 20)           # stable physical/damage draws
        word(save, 0x4A0, 392)          # one-monster test formation

        # Ability 38 applies effect 67 (力量加強).  The selected random-buffer
        # window gives its minimum duration: two deliberately non-lethal
        # physical actions followed by direct damage item 192.  The item
        # returns through 1138 with pose zero when 0c41 enters 0da7.
        word(save, actor + 0x0C, 1)
        word(save, actor + 0x0E, 60000)
        word(save, actor + 0x2D, 60000)
        word(save, actor + 0x2F, 60000)
        word(save, actor + 0x31, 1)
        word(save, actor + 0x33, 60000)
        word(save, actor + 0x35, 60000)
        word(save, actor + 0x37, 60000)
        word(save, actor + 0x41, 1)
        word(save, actor + 0x43, 60000)
        word(save, actor + 0x55, 60000)
        word(save, actor + 0x57, 60000)
        word(save, actor + 0x5D, 60000)
        word(save, actor + 0x5F, 60000)
        word(save, actor + 0x6D, 38)
        word(save, actor + 0x6E, 1)
        for offset in range(0x382, 0x3E6, 2):
            word(save, offset, 0)
        word(save, 0x382, 192)          # 疾電符, direct selector 33h

        save_digest = sha256(save)
        mapz_digest = sha256((args.output / "MAPZ.DA1").read_bytes())
        name_digest = sha256((args.output / "NAME1.DSK").read_bytes())
        if save_digest != EXPECTED_SAVE:
            raise ValueError(
                f"FIG player-buff-expiry-item SAVE differs: {save_digest}")
        if mapz_digest != EXPECTED_MAPZ or name_digest != EXPECTED_NAME:
            raise ValueError(
                "FIG player-buff-expiry-item companion save files differ")
        save_path.write_bytes(save)
        print(
            "FIG player-buff-expiry-item fixture: "
            f"SAVE={save_digest} MAPZ={mapz_digest} NAME={name_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG player-buff-expiry-item fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
