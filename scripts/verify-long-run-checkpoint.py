#!/usr/bin/env python3
"""Verify the hashed in-progress macOS/native/Edge/WebKit soak checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
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
        matrix = load(args.checkpoint / "matrix-report.json")
        native_path = args.checkpoint / "native-log.json"
        edge_path = args.checkpoint / "edge-browser-log.json"
        webkit_path = args.checkpoint / "webkit-browser-log.json"
        native = load(native_path)
        edge = load(edge_path)
        webkit = load(webkit_path)
        if matrix.get("schema_version") != 1 or \
                matrix.get("kind") != "swd2_long_run_checkpoint" or \
                matrix.get("status") != "in_progress":
            raise ValueError("checkpoint schema/status differs")
        coverage = matrix.get("coverage")
        if not isinstance(coverage, dict) or \
                coverage.get("native_process_replays", 0) < 250 or \
                coverage.get("edge_idbfs_restart_cycles", 0) < 100 or \
                coverage.get("webkit_idbfs_restart_cycles", 0) < 1000:
            raise ValueError("checkpoint coverage is below the registered floor")
        if native.get("status") != "verified" or \
                native.get("cycles") != coverage["native_process_replays"] or \
                edge.get("status") != "verified" or \
                edge.get("idbfs", {}).get("cycles") != \
                coverage["edge_idbfs_restart_cycles"] or \
                webkit.get("status") != "verified" or \
                webkit.get("idbfs", {}).get("cycles") != \
                coverage["webkit_idbfs_restart_cycles"]:
            raise ValueError("checkpoint component counts/status differ")
        if edge.get("gesture", {}).get("deliveries_after_release_fence") != 0:
            raise ValueError("Edge touch delivery continued after release fence")
        artifacts = matrix.get("artifacts")
        expected = {
            "native_log_sha256": sha256(native_path),
            "edge_browser_log_sha256": sha256(edge_path),
            "webkit_browser_log_sha256": sha256(webkit_path),
        }
        if artifacts != expected:
            raise ValueError("checkpoint artifact digest differs")
        remaining = matrix.get("remaining")
        if not isinstance(remaining, list) or len(remaining) < 4:
            raise ValueError("checkpoint incorrectly omits remaining platforms")
        print(
            "Long-run checkpoint: OK/in progress "
            f"({coverage['native_process_replays']} native, "
            f"{coverage['edge_idbfs_restart_cycles']} Edge, "
            f"{coverage['webkit_idbfs_restart_cycles']} WebKit cycles)"
        )
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"long-run checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
