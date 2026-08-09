#!/usr/bin/env python3
"""Copy the original Q checkpoint into an isolated writable slot one."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


FILES = {
    "SAVE.DAQ": ("SAVE.DA1", "ba9a6fef7ff12697cfd72e3197125a18995fd78388f766b18f508f3f08f49099"),
    "MAPZ.DAQ": ("MAPZ.DA1", "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917"),
    "NAMEQ.DSK": ("NAME1.DSK", "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba"),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    for source, (target, expected) in FILES.items():
        data = (args.game / source).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            parser.error(f"{source} is not the supported release checkpoint")
        (args.output / target).write_bytes(data)
    print("RPG Record fixture: pristine Q checkpoint copied to writable slot 1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
