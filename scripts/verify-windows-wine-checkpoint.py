#!/usr/bin/env python3
"""Verify the pinned Wine execution boundary for the Windows AMD64 CLI."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


EXPECTED_REPLAY = {
    "frames": 113,
    "inputs": 8,
    "transitions": 2,
    "final_state_fnv1a64": "54098cf0ca14338b",
    "final_mapz_fnv1a64": "827f0f1b725a0958",
    "video_fnv1a64": "014ecefd7f3b8a7b",
    "audio_fnv1a64": "b4be901ad5ab6a4c",
}
EXPECTED_FRAME_BYTES = 7_320_636
EXPECTED_FRAME_SHA256 = (
    "d2703c591e4a0e1d6ded813b5a413b62a704277a772b022ac6c2c88f2ff6c2cd"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"not a JSON object: {path}")
    return value


def checked(command: list[str], label: str) -> None:
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise ValueError(f"{label} failed:\n{result.stdout[-3000:]}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    try:
        native_trace_path = args.checkpoint / "wine-native-trace.json"
        windows_trace_path = args.checkpoint / "wine-windows-trace.json"
        comparison_path = args.checkpoint / "wine-comparison.json"
        native_run_path = args.checkpoint / "wine-native-run.log"
        windows_run_path = args.checkpoint / "wine-windows-run.log"
        execution_path = args.checkpoint / "wine-execution-log.json"
        build_path = args.checkpoint / "native-log.json"
        execution = load(execution_path)
        build = load(build_path)
        native_trace = load(native_trace_path)
        windows_trace = load(windows_trace_path)
        comparison = load(comparison_path)

        if execution.get("schema_version") != 1 or \
                execution.get("kind") != \
                "swd2_windows_wine_execution_checkpoint" or \
                execution.get("status") != "verified" or \
                execution.get("scope") != "emulated_not_windows_host" or \
                execution.get("container_architecture") != "x86_64" or \
                not str(execution.get("wine_version", "")).startswith("wine-10."):
            raise ValueError("Windows Wine execution identity differs")
        if not isinstance(execution.get("source_commit"), str) or \
                re.fullmatch(r"[0-9a-f]{40}", execution["source_commit"]) is None or \
                execution["source_commit"] != build.get("source_commit") or \
                execution.get("windows_executable_sha256") != \
                build.get("executable_sha256"):
            raise ValueError("Windows Wine source/executable identity differs")
        limitation = execution.get("limitation")
        if not isinstance(limitation, str) or \
                "not a physical Windows-host" not in limitation or \
                "not" not in limitation:
            raise ValueError("Windows Wine checkpoint lost its scope limitation")

        expected_hashes = {
            "native_trace_sha256": sha256(native_trace_path),
            "windows_trace_sha256": sha256(windows_trace_path),
            "comparison_sha256": sha256(comparison_path),
            "native_run_log_sha256": sha256(native_run_path),
            "windows_run_log_sha256": sha256(windows_run_path),
        }
        for name, expected in expected_hashes.items():
            if execution.get(name) != expected:
                raise ValueError(f"Windows Wine artifact hash differs: {name}")
        if execution.get("frame_capture_sha256") != EXPECTED_FRAME_SHA256 or \
                execution.get("frame_capture_bytes") != EXPECTED_FRAME_BYTES or \
                execution.get("exact_native_frame_capture_match") is not True or \
                execution.get("replay") != EXPECTED_REPLAY:
            raise ValueError("Windows Wine deterministic replay boundary differs")

        if native_trace != windows_trace:
            raise ValueError("committed native and Windows traces differ")
        if native_trace.get("video", {}).get("frames") != 113 or \
                native_trace.get("input", {}).get("consumed") != 8:
            raise ValueError("committed Windows replay summary differs")
        if comparison.get("status") != "verified" or \
                not isinstance(comparison.get("checks"), dict) or \
                not comparison["checks"] or \
                not all(comparison["checks"].values()) or \
                comparison.get("original_sha256") != sha256(native_trace_path) or \
                comparison.get("rewrite_sha256") != sha256(windows_trace_path):
            raise ValueError("Windows/native live comparison report differs")
        checked([
            "python3", str(root / "scripts/verify-replay-trace.py"),
            str(windows_trace_path), "--min-frames", "113",
        ], "Windows replay trace validation")
        checked([
            "python3", str(root / "scripts/compare-replay-traces.py"),
            str(native_trace_path), str(windows_trace_path),
            "--verify-report", str(comparison_path),
        ], "Windows/native replay comparison")
        if "MEO.EXE -> MT" not in windows_run_path.read_text(encoding="utf-8") or \
                "single-process frames=113" not in \
                windows_run_path.read_text(encoding="utf-8"):
            raise ValueError("Windows Wine process log is incomplete")

        print(
            "Windows Wine checkpoint: OK/emulated (113 exact frames, "
            "native trace and capture byte parity)")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"Windows Wine checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
