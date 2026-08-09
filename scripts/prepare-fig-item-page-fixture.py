#!/usr/bin/env python3
"""Create the deterministic IF-entry save for FIG's item information page."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "670eb2287c7f363fad62ad6ba99327a7e69d3eee947309bdf2306ff4044d9c3f"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        for name in ("MAPZ.DA1", "NAME1.DSK"):
            shutil.copy2(args.game / name, args.output / name)
        data = bytearray((args.game / "SAVE.DA1").read_bytes())
        data[0x4A0:0x4A2] = (392).to_bytes(2, "little")
        for offset in range(0x382, 0x3E6, 2):
            data[offset:offset + 2] = b"\0\0"
        data[0x382:0x384] = (51).to_bytes(2, "little")
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG item-page fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG item 51 page fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG item-page fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
