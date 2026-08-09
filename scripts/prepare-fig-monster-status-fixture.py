#!/usr/bin/env python3
"""Create deterministic IF-entry fixtures for all five monster status icons."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


ABILITY_IDS = (6, 72, 93, 56, 58)
EXPECTED = {
    6: "d6f50b93ae65ed6c403f894d86118f28805149b2a229a5b3e6f3aa1809b19730",
    72: "38010b72f75b2e514d6b00ec991bc18c7c27441bcc15e749b6822592ae78f75e",
    93: "d4e028f693215cc47d8ffa412c023162763ae9c0f5386b08778b21d2f68ba77a",
    56: "dc84bb7c224bc5c937389049941c1971b180eb5a4ce193a64fbfc322dd4a731e",
    58: "5c498d1a673fccb99e4a06af6b59dd518a15b139202fc1576a7487cd506f8879",
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
        for ability_id in ABILITY_IDS:
            case = args.output / f"ability-{ability_id}"
            case.mkdir()
            for name in ("MAPZ.DA1", "NAME1.DSK"):
                shutil.copy2(args.game / name, case / name)
            data = bytearray(original)
            word(data, 0x10, 1)              # one visible/commandable actor
            word(data, 0x4A0, 0x04D4)        # formation with monster 338
            word(data, actor + 0x08, 0)      # clear status/death bits
            word(data, actor + 0x0E, 60000)  # survive the monster's response
            word(data, actor + 0x2D, 60000)
            word(data, actor + 0x2F, 60000)
            word(data, actor + 0x35, 1000)   # class-two resource
            word(data, actor + 0x37, 1000)
            word(data, actor + 0x55, 1000)   # class-four resource
            word(data, actor + 0x57, 1000)
            word(data, actor + 0x5D, 0xFFFF) # act before monster 338
            # Keep the release save's following learned slot intact. Only the
            # first byte is replaced, matching the staged original captures.
            data[actor + 0x6D] = ability_id
            actual = hashlib.sha256(data).hexdigest()
            if actual != EXPECTED[ability_id]:
                raise ValueError(
                    f"FIG monster-status ability {ability_id} differs: {actual}")
            (case / "SAVE.DA1").write_bytes(data)
            print(f"FIG monster-status ability {ability_id}: {actual}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG monster-status fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
