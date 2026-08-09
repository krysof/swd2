#!/usr/bin/env python3
"""Verify --no-save suppresses both explicit Record and exit persistence."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_root", type=Path)
    parser.add_argument("save_root", type=Path)
    args = parser.parse_args()

    for stem in ("SAVE.DA1", "MAPZ.DA1", "NAME1.DSK"):
        if (args.save_root / stem).read_bytes() != (args.game_root / stem).read_bytes():
            raise SystemExit(f"--no-save changed the seeded {stem}")
    for stem in ("SAVE.DA4", "MAPZ.DA4", "NAME4.DSK"):
        if (args.save_root / stem).exists():
            raise SystemExit(f"--no-save created explicit Record target {stem}")
    print("--no-save suppressed explicit Record and exit checkpoint writes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
