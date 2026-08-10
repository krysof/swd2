#!/usr/bin/env python3
"""Create the deterministic IF-entry save for direct item 186/effect 69h."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


ITEM_IDS = (186, 226)
EXPECTED = {
    186: "c084db5d4a994c1be4a6a51369fde07a2db663da2bb16bf4ed9f26e22d31b48d",
    226: "16ed95fddb4c9d433d0745af7f79cb218e9658d1fc86bfea7a71f272a579b2bc",
}


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
        original = bytearray((args.game / "SAVE.DA1").read_bytes())
        actor = 0x106
        for item_id in ITEM_IDS:
            case = args.output / f"item-{item_id}"
            case.mkdir()
            for name in ("MAPZ.DA1", "NAME1.DSK"):
                shutil.copy2(args.game / name, case / name)
            data = bytearray(original)
            word(data, 0x10, 1)              # one commandable actor
            word(data, 0x4A0, 392)           # one monster; no introduction
            word(data, actor + 0x08, 0)      # clear status/death bits
            word(data, actor + 0x2D, 1000)   # HP/current maximum
            word(data, actor + 0x2F, 1000)
            word(data, actor + 0x35, 1000)   # secondary current/maximum
            word(data, actor + 0x37, 1000)
            word(data, actor + 0x55, 1000)   # ability resource current/maximum
            word(data, actor + 0x57, 1000)
            word(data, actor + 0x5D, 1000)   # player acts first
            for offset in range(0x382, 0x3E6, 2):
                word(data, offset, 0)
            word(data, 0x382, item_id)

            actual = hashlib.sha256(data).hexdigest()
            if actual != EXPECTED[item_id]:
                raise ValueError(
                    f"FIG item-status {item_id} save differs: {actual}")
            (case / "SAVE.DA1").write_bytes(data)
            print(f"FIG item-status {item_id} fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG item-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
