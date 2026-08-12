#!/usr/bin/env python3
"""Create the deterministic ORC 094h introduction-without-prompt fixture."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED = "722396dca85ed04e7893a1713b31ddf1b34ddee98b66c68f08ba406ea4236962"


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
        data[0x4A0:0x4A2] = (0x094).to_bytes(2, "little")
        actual = hashlib.sha256(data).hexdigest()
        if actual != EXPECTED:
            raise ValueError(f"FIG no-prompt introduction fixture differs: {actual}")
        (args.output / "SAVE.DA1").write_bytes(data)
        print(f"FIG no-prompt introduction fixture: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG no-prompt introduction fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
