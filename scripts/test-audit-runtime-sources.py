#!/usr/bin/env python3
"""Regression-test the runtime placeholder marker domain."""

from __future__ import annotations

import importlib.util
from pathlib import Path


def main() -> int:
    path = Path(__file__).with_name("audit-runtime-sources.py")
    spec = importlib.util.spec_from_file_location("audit_runtime_sources", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load runtime source audit")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    pattern = module.PLACEHOLDER_MARKER
    marked = (
        "TODO", "fixme", "XXX", "stub", "PLACEHOLDER",
        "NOT IMPLEMENTED", "not_implemented", "not-implemented",
        "approximate", "APPROXIMATED", "approximates", "approximating",
        "approximation", "approximations",
    )
    allowed = (
        "implementation", "stubble", "TODOList", "exact original parity",
        "generic native UI replacement is forbidden",
    )
    missed = [value for value in marked if pattern.search(value) is None]
    false_positives = [value for value in allowed if pattern.search(value)]
    if missed or false_positives:
        raise RuntimeError(
            f"placeholder marker domain differs: missed={missed}, "
            f"false_positives={false_positives}")
    print(
        f"runtime source placeholder markers: {len(marked)} rejected forms, "
        f"{len(allowed)} allowed controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
