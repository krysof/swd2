#!/usr/bin/env python3
"""Unit-check the failure-closed DOSBox-X AUTOTYPE log boundary."""

from __future__ import annotations

import importlib.util
from pathlib import Path


def main() -> int:
    source = Path(__file__).with_name("capture-original-dosbox.py")
    spec = importlib.util.spec_from_file_location("capture_original_dosbox", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load capture-original-dosbox.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    module.validate_autotype_log(
        "LOG: Mapper keyboard layout is now us (US English)\n"
        "LOG: Started capturing video\nLOG: Stopped capturing video.\n")
    rejected = False
    try:
        module.validate_autotype_log(
            "LOG: Stopped capturing video.\n"
            "Message was: MAPPER: Couldn't find a button named 'right', "
            "stopping.\n")
    except RuntimeError as error:
        rejected = "AUTOTYPE stream" in str(error) and "right" in str(error)
    if not rejected:
        raise RuntimeError("invalid AUTOTYPE mapper token was accepted")
    print("DOSBox-X capture log: incomplete AUTOTYPE streams fail closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
