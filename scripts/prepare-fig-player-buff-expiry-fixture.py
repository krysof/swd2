#!/usr/bin/env python3
"""Stage a deterministic FIG player attack-buff natural-expiry battle."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "6d450a222d01e64882de515c87d5f7d0294c30389b6f4c3f875ac467361c0d51"
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
        word(save, 0x49E, 20)           # stable physical-damage draw
        word(save, 0x4A0, 392)          # one-monster test formation

        # Keep the fixture alive for eight physical rounds while making the
        # attacks non-lethal.  Ability 38 applies effect 67 (力量加強); its
        # eighth player turn naturally reaches FIG 0c41/0da7.
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

        save_digest = sha256(save)
        mapz_digest = sha256((args.output / "MAPZ.DA1").read_bytes())
        name_digest = sha256((args.output / "NAME1.DSK").read_bytes())
        if save_digest != EXPECTED_SAVE:
            raise ValueError(
                f"FIG player-buff-expiry SAVE differs: {save_digest}")
        if mapz_digest != EXPECTED_MAPZ or name_digest != EXPECTED_NAME:
            raise ValueError(
                "FIG player-buff-expiry companion save files differ")
        save_path.write_bytes(save)
        print(
            "FIG player-buff-expiry fixture: "
            f"SAVE={save_digest} MAPZ={mapz_digest} NAME={name_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG player-buff-expiry fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
