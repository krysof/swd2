#!/usr/bin/env python3
"""Prove the shipped FIG 5f82 timer bypass is unreachable."""

from __future__ import annotations

import argparse
from pathlib import Path


DATA_SEGMENT_PARAGRAPHS = 0x0E03
BYPASS_FLAG = 0x3CA4


def load_image(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1C:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid MZ header")
    return executable[header:]


def require_bytes(image: bytes, offset: int, expected_hex: str,
                  label: str) -> None:
    expected = bytes.fromhex(expected_hex)
    if image[offset:offset + len(expected)] != expected:
        raise ValueError(f"FIG {label} machine code differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    args = parser.parse_args()
    try:
        image = load_image((args.game / "FIG.EXE").read_bytes())

        # The entry point establishes DS=0e03h before any FIG code runs.
        require_bytes(image, 0x0000, "b8030e8ed8", "data-segment setup")
        # 383d is the unconditional AX-tick hold used by fixed cards.
        require_bytes(
            image, 0x383D,
            "c706a53c00003906a53c72fac3",
            "unconditional timer hold")
        # 5f82 checks DATA:3ca4, otherwise waits for DATA:3ca5 to reach
        # DATA:3ca9, then clears the counter.
        require_bytes(
            image, 0x5F82,
            "a1a93c803ea43c0174063906a53c72fac706a53c0000c3",
            "conditional timer hold")
        # The medium-flight loop renders, presents, calls 5f82, flips, then
        # advances the next position.
        require_bytes(
            image, 0x5B61,
            "50e8c9e0e8d10ae81704e8681358",
            "medium-flight timer call")

        flag_offset = DATA_SEGMENT_PARAGRAPHS * 16 + BYPASS_FLAG
        if flag_offset >= len(image) or image[flag_offset] != 0:
            raise ValueError("FIG DATA:3ca4 does not start cleared")

        # The little-endian absolute address 3ca4h occurs once in the entire
        # load image, as the CMP operand above.  There is no direct writer (or
        # second reader) that can make the shipped release take the bypass.
        references = [index for index in range(len(image) - 1)
                      if image[index:index + 2] == b"\xa4\x3c"]
        if references != [0x5F87]:
            raise ValueError(
                "FIG DATA:3ca4 reference domain differs: " +
                repr([hex(index) for index in references]))

        print(
            "FIG timer machine code: DATA:3ca4 starts at zero, has one "
            "read-only literal reference, and 5f82 always waits")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"FIG timer machine code: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
