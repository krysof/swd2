#!/usr/bin/env python3
"""Install the missing Tianhuo-fan battle sprite from the shipped iron fan."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="SWD2 game resource directory")
    args = parser.parse_args()

    source = args.game / "SW" / "SW124.RSK"
    target = args.game / "SW" / "SW062.RSK"
    if not source.is_file():
        parser.error(f"shipped iron-fan resource is missing: {source}")

    if target.exists() and target.read_bytes() != source.read_bytes():
        parser.error(f"refusing to replace a different existing resource: {target}")
    if not target.exists():
        shutil.copy2(source, target)

    if target.read_bytes() != source.read_bytes():
        raise RuntimeError("Tianhuo-fan resource copy verification failed")
    print(f"installed {target} from {source} (sha256={sha256(target)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
