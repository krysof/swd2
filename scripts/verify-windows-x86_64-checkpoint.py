#!/usr/bin/env python3
"""Verify the hashed, explicitly in-progress Windows x86-64 checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


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
        log_path = args.checkpoint / "build.log"
        matrix_path = args.checkpoint / "matrix-report.json"
        wine_execution_path = args.checkpoint / "wine-execution-log.json"
        wine_native_trace_path = args.checkpoint / "wine-native-trace.json"
        wine_windows_trace_path = args.checkpoint / "wine-windows-trace.json"
        wine_comparison_path = args.checkpoint / "wine-comparison.json"
        native = load(native_path)
        matrix = load(matrix_path)
        log = log_path.read_text(encoding="utf-8")
        if native.get("schema_version") != 1 or \
                native.get("kind") != "swd2_windows_cross_build_checkpoint" or \
                native.get("status") != "verified" or \
                native.get("compiler_target") != "x86_64-w64-mingw32" or \
                native.get("build_type") != "Release" or \
                native.get("sdl_frontend") != "disabled_for_cross_build" or \
                native.get("mingw_runtime") != "statically_linked" or \
                native.get("warning_count") != 0 or \
                native.get("pe_machine") != "0x8664" or \
                not isinstance(native.get("pe_sections"), int) or \
                native["pe_sections"] < 1 or \
                not isinstance(native.get("executable_size"), int) or \
                native["executable_size"] < 1_000_000:
            raise ValueError("Windows native checkpoint boundary differs")
        imports = native.get("pe_imports")
        if not isinstance(imports, list) or "KERNEL32.dll" not in imports or \
                any(not isinstance(name, str) or name.lower().startswith(
                    ("libgcc_", "libstdc++-", "libwinpthread-"))
                    for name in imports):
            raise ValueError("Windows PE compiler-runtime import boundary differs")
        if not isinstance(native.get("source_commit"), str) or \
                re.fullmatch(r"[0-9a-f]{40}", native["source_commit"]) is None or \
                not isinstance(native.get("compiler"), str) or \
                "mingw" not in native["compiler"].lower():
            raise ValueError("Windows compiler/source identity differs")
        if "warning:" in log.lower() or \
                "Linking CXX executable src/swd2_rewrite.exe" not in log or \
                native.get("build_log_sha256") != sha256(log_path) or \
                not isinstance(native.get("executable_sha256"), str) or \
                re.fullmatch(r"[0-9a-f]{64}", native["executable_sha256"]) is None:
            raise ValueError("Windows build log/artifact identity differs")

        if matrix.get("schema_version") != 1 or \
                matrix.get("kind") != "swd2_windows_platform_checkpoint" or \
                matrix.get("status") != "in_progress":
            raise ValueError("Windows matrix schema/status differs")
        if matrix.get("coverage") != {
                "platform": "windows",
                "architecture": "x86_64",
                "native_release_cross_builds": 1,
                "pe_cli_outputs": 1,
                "wine_pe_replays": 1,
                "native_equivalence_replays": 1,
                "exact_indexed_frame_matches": 113,
        }:
            raise ValueError("Windows matrix coverage differs")
        if matrix.get("artifacts") != {
                "native_log_sha256": sha256(native_path),
                "build_log_sha256": sha256(log_path),
                "wine_execution_log_sha256": sha256(wine_execution_path),
                "wine_native_trace_sha256": sha256(wine_native_trace_path),
                "wine_windows_trace_sha256": sha256(wine_windows_trace_path),
                "wine_comparison_sha256": sha256(wine_comparison_path),
        }:
            raise ValueError("Windows matrix artifact hashes differ")
        remaining = matrix.get("remaining")
        if not isinstance(remaining, list) or len(remaining) < 4 or \
                not any("SDL2" in item for item in remaining) or \
                not any("physical Windows-hosted execution" in item
                        for item in remaining):
            raise ValueError("Windows checkpoint incorrectly claims final coverage")
        print(
            "Windows x86-64 checkpoint: OK/in progress "
            f"({native['executable_size']} byte PE, zero warnings)")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"Windows x86-64 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
