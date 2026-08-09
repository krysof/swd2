#!/usr/bin/env python3
"""Create the two deterministic saves used by equipment-error replays."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


EXPECTED = {
    "restricted": "1b3253f45283a4bdfaa4e72828a9ef6251a43600b8be4683724d2b7b3d16dfd0",
    "twohand": "4ba4f8ae00fd82fc2ef1866b49e1fd07270f75ca60978aa5772e5ac440aea480",
}


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def write_checked(path: Path, data: bytearray, expected: str) -> None:
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f"equipment-error fixture differs: {actual}")
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)

        # Reuse the already audited four-member SWRO7 party fixture rather
        # than maintaining a second copy of its map/path/actor mutations.
        restricted = args.output / "restricted"
        subprocess.run([
            sys.executable,
            str(Path(__file__).with_name("prepare-rpg-party-target-fixture.py")),
            str(args.game), str(restricted),
        ], check=True)
        restricted_save = restricted / "SAVE.DA1"
        data = bytearray(restricted_save.read_bytes())
        word(data, 0x382, 166)  # ITEM +04 mask forbids actor identities 0/24/36.
        write_checked(restricted_save, data, EXPECTED["restricted"])

        twohand = args.output / "twohand"
        shutil.copytree(restricted, twohand)
        twohand_save = twohand / "SAVE.DA1"
        data = bytearray(twohand_save.read_bytes())
        word(data, 0x382, 117)          # category-nine two-handed incoming item
        word(data, 0x106 + 0x14, 117)  # occupied left hand
        word(data, 0x106 + 0x16, 118)  # independently occupied right hand
        data[0x106 + 0x2c] = 0         # not an existing mirrored pair
        write_checked(twohand_save, data, EXPECTED["twohand"])
        print(
            "RPG equipment-error fixtures: actor restriction/category mismatch "
            "and occupied-hand conflict")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG equipment-error fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
