#!/usr/bin/env python3
"""Run a fail-closed local native/browser stability checkpoint.

This is deliberately not the final long_run manifest. It repeats a
deterministic native module-transition checkpoint and the real Edge/WebKit IDBFS
restart boundaries, records byte-stable outputs, and writes status=in_progress
until the remaining OS/physical-device and multi-hour matrix is supplied.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import platform
import shutil
import subprocess
import time
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def checked(command: list[str], label: str) -> None:
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} exited {result.returncode}:\n{result.stdout[-4000:]}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--executable", type=Path,
                        default=root / "build/src/swd2_rewrite")
    parser.add_argument("--game", type=Path, default=root / "game")
    parser.add_argument("--site", type=Path, default=root / "build-wasm/site")
    parser.add_argument("--playwright", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--native-cycles", type=int, default=250)
    parser.add_argument("--edge-idbfs-cycles", type=int, default=100)
    parser.add_argument("--webkit-idbfs-cycles", type=int, default=1000)
    args = parser.parse_args()

    try:
        for count, label in (
            (args.native_cycles, "native cycles"),
            (args.edge_idbfs_cycles, "Edge cycles"),
            (args.webkit_idbfs_cycles, "WebKit cycles"),
        ):
            if count < 1 or count > 10_000:
                raise ValueError(f"{label} must be from 1 through 10000")
        for path, label in (
            (args.executable, "native executable"),
            (args.game / "SAVE.DA1", "game data"),
            (args.site / "index.wasm", "WASM site"),
            (args.playwright / "index.mjs", "Playwright package"),
        ):
            if not path.exists():
                raise ValueError(f"{label} is absent: {path}")
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        save_root = args.output / "native-saves"
        save_root.mkdir()
        for name in ("SAVE.DA1", "MAPZ.DA1", "NAME1.DSK"):
            shutil.copy2(args.game / name, save_root / name)

        native_trace = args.output / "native-trace.json"
        replay = root / "scripts/replay-smoke.txt"
        expected_trace_digest: str | None = None
        native_start = time.monotonic()
        for cycle in range(args.native_cycles):
            checked([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(save_root), "--slot", "1", "--no-save",
                "--run-replay", str(replay),
                "--trace-output", str(native_trace),
            ], f"native replay cycle {cycle}")
            digest = sha256(native_trace)
            if expected_trace_digest is None:
                expected_trace_digest = digest
            elif digest != expected_trace_digest:
                raise ValueError(
                    f"native replay cycle {cycle} was nondeterministic: {digest}"
                )
        native_elapsed = time.monotonic() - native_start
        native_log = {
            "schema_version": 1,
            "kind": "swd2_native_replay_soak",
            "status": "verified",
            "cycles": args.native_cycles,
            "elapsed_seconds": round(native_elapsed, 6),
            "trace_sha256": expected_trace_digest,
            "replay_sha256": sha256(replay),
            "executable_sha256": sha256(args.executable),
        }
        native_log_path = args.output / "native-log.json"
        write_json(native_log_path, native_log)
        native_trace.unlink()
        shutil.rmtree(save_root)

        edge_report = args.output / "edge-browser-log.json"
        checked([
            "node", str(root / "scripts/verify-wasm-browser-input.mjs"),
            str(args.site), "--idbfs-cycles", str(args.edge_idbfs_cycles),
            "--output", str(edge_report),
        ], "Edge browser soak")

        webkit_report = args.output / "webkit-browser-log.json"
        checked([
            "node", str(root / "scripts/verify-wasm-webkit-idbfs.mjs"),
            str(args.site), "--playwright", str(args.playwright),
            "--cycles", str(args.webkit_idbfs_cycles),
            "--output", str(webkit_report),
        ], "WebKit browser soak")

        edge = json.loads(edge_report.read_text(encoding="utf-8"))
        webkit = json.loads(webkit_report.read_text(encoding="utf-8"))
        if edge.get("status") != "verified" or webkit.get("status") != "verified":
            raise ValueError("a browser soak report is not verified")
        matrix = {
            "schema_version": 1,
            "kind": "swd2_long_run_checkpoint",
            "status": "in_progress",
            "captured_at": datetime.datetime.now(datetime.timezone.utc).isoformat(
                timespec="seconds"
            ),
            "host": {
                "platform": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
            },
            "coverage": {
                "native_process_replays": args.native_cycles,
                "edge_idbfs_restart_cycles": args.edge_idbfs_cycles,
                "webkit_idbfs_restart_cycles": args.webkit_idbfs_cycles,
            },
            "artifacts": {
                "native_log_sha256": sha256(native_log_path),
                "edge_browser_log_sha256": sha256(edge_report),
                "webkit_browser_log_sha256": sha256(webkit_report),
            },
            "remaining": [
                "multi-hour active gameplay/audio soak",
                "Windows and Linux native backends",
                "physical iOS/Android browser restart and touch",
                "physical gamepad matrix",
            ],
        }
        matrix_path = args.output / "matrix-report.json"
        write_json(matrix_path, matrix)
        print(
            "Long-run checkpoint: IN PROGRESS "
            f"({args.native_cycles} native, {args.edge_idbfs_cycles} Edge, "
            f"{args.webkit_idbfs_cycles} WebKit cycles)"
        )
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"long-run checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
