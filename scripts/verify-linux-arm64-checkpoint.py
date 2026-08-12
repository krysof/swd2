#!/usr/bin/env python3
"""Verify the hashed, in-progress Linux ARM64 native checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


BASE_IMAGE = (
    "debian@sha256:3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"not a JSON object: {path}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path)
    args = parser.parse_args()
    try:
        native_path = args.checkpoint / "native-log.json"
        log_path = args.checkpoint / "ctest.log"
        matrix_path = args.checkpoint / "matrix-report.json"
        native = load(native_path)
        matrix = load(matrix_path)
        log = log_path.read_text(encoding="utf-8")
        if native.get("schema_version") != 1 or \
                native.get("kind") != "swd2_linux_native_checkpoint" or \
                native.get("status") != "verified" or \
                native.get("container_base") != BASE_IMAGE or \
                native.get("platform") != "linux" or \
                native.get("architecture") != "aarch64" or \
                native.get("build_type") != "Release" or \
                native.get("warning_count") != 0 or \
                native.get("tests_failed") != 0 or \
                not isinstance(native.get("tests_passed"), int) or \
                native["tests_passed"] < 508:
            raise ValueError("Linux native checkpoint boundary differs")
        if not isinstance(native.get("source_commit"), str) or \
                re.fullmatch(r"[0-9a-f]{40}", native["source_commit"]) is None or \
                not isinstance(native.get("container_image_id"), str) or \
                not native["container_image_id"].startswith("sha256:"):
            raise ValueError("Linux native identity differs")
        summary = (
            f"100% tests passed, 0 tests failed out of "
            f"{native['tests_passed']}")
        if summary not in log or "warning:" in log or \
                native.get("ctest_log_sha256") != sha256(log_path):
            raise ValueError("Linux CTest log differs")

        if matrix.get("schema_version") != 1 or \
                matrix.get("kind") != "swd2_linux_platform_checkpoint" or \
                matrix.get("status") != "in_progress":
            raise ValueError("Linux matrix schema/status differs")
        coverage = matrix.get("coverage")
        if not isinstance(coverage, dict) or \
                coverage.get("platform") != "linux" or \
                coverage.get("architecture") != "aarch64" or \
                coverage.get("native_release_builds") != 1 or \
                coverage.get("native_ctest_passes") != native["tests_passed"]:
            raise ValueError("Linux matrix coverage differs")
        if matrix.get("artifacts") != {
                "native_log_sha256": sha256(native_path),
                "ctest_log_sha256": sha256(log_path)}:
            raise ValueError("Linux matrix artifact hashes differ")
        remaining = matrix.get("remaining")
        if not isinstance(remaining, list) or len(remaining) < 4 or \
                not any("Windows" in item for item in remaining):
            raise ValueError("Linux checkpoint incorrectly claims final matrix")
        print(
            "Linux ARM64 checkpoint: OK/in progress "
            f"({native['tests_passed']} tests, zero warnings)")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"Linux ARM64 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
