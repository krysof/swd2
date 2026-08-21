#!/usr/bin/env python3
"""Exercise complete, partial, stale-output and CLI boundary-capture rules."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], expected: int) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != expected:
        raise RuntimeError(
            f"command returned {result.returncode}, expected {expected}:\n"
            f"{' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rewrite", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("replay", type=Path)
    parser.add_argument("verifier", type=Path)
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="swd2-boundary-test-") as raw:
            root = Path(raw)
            complete = root / "complete"
            base = [
                str(args.rewrite), "--game", str(args.game),
                "--save-dir", str(root / "saves"), "--no-save",
            ]
            run(base + [
                "--run-replay", str(args.replay),
                "--boundary-output", str(complete),
            ], 0)
            run([
                sys.executable, str(args.verifier), str(complete),
                "--expected-invocations", "2", "--expected-figures", "0",
            ], 0)

            stale = run(base + [
                "--run-replay", str(args.replay),
                "--boundary-output", str(complete),
            ], 1)
            if "not empty" not in stale.stderr:
                raise RuntimeError("non-empty boundary output was not rejected")

            short_replay = root / "short.txt"
            short_replay.write_text("WAIT:CONFIRM\n", encoding="ascii")
            partial = root / "partial"
            failed = run(base + [
                "--run-replay", str(short_replay),
                "--boundary-output", str(partial),
            ], 1)
            if "implicit quit" not in failed.stderr:
                raise RuntimeError("exhausted replay did not fail closed")
            if (partial / "manifest.json").exists():
                raise RuntimeError("failed replay emitted a complete manifest")
            partial_manifest = json.loads(
                (partial / "manifest.partial.json").read_text(encoding="utf-8"))
            if (partial_manifest.get("status") != "incomplete" or
                    partial_manifest.get("transition_count") is not None or
                    partial_manifest.get("input", {}).get("remaining") != 0):
                raise RuntimeError("partial manifest does not identify incomplete evidence")

            script_output = root / "script-output"
            scripted = run(base + [
                "--run-script", "CONFIRM,QUIT",
                "--boundary-output", str(script_output),
            ], 1)
            if "requires strict --run-replay" not in scripted.stderr:
                raise RuntimeError("run-script boundary output was not rejected")
            if script_output.exists():
                raise RuntimeError("invalid CLI created a boundary output directory")
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"module boundary capture tests: FAIL: {error}", file=sys.stderr)
        return 1
    print("module boundary capture tests: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
