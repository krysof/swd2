#!/usr/bin/env python3
"""Verify that a strict replay failure preserves its consumed-prefix trace."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="swd2-replay-failure-") as temp:
        root = Path(temp)
        replay = root / "extra-input.txt"
        trace = root / "partial-trace.json"
        replay.write_text("POLL:QUIT\nPOLL:NONE\n", encoding="ascii")
        result = subprocess.run(
            [
                str(args.executable),
                "--game", str(args.game),
                "--no-save",
                "--start-marker", "OC",
                "--run-replay", str(replay),
                "--trace-output", str(trace),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        expected_error = (
            "replay stopped after consuming 1 of 2 boundary-locked inputs")
        if result.returncode == 0 or expected_error not in result.stderr:
            raise SystemExit(
                "strict replay did not report its exact consumed prefix:\n" +
                result.stdout + result.stderr)
        if not trace.is_file():
            raise SystemExit("strict replay failure did not write a partial trace")
        payload = json.loads(trace.read_text(encoding="utf-8"))
        expected_input = {
            "total": 2,
            "consumed": 1,
            "remaining": 1,
            "implicit_quit_calls": 0,
        }
        if payload.get("input") != expected_input:
            raise SystemExit(
                f"strict replay partial input summary differs: {payload.get('input')!r}")
        checkpoints = payload.get("input_checkpoints")
        if not isinstance(checkpoints, list) or len(checkpoints) != 1 or \
                checkpoints[0].get("boundary") != "POLL" or \
                checkpoints[0].get("action") != "QUIT":
            raise SystemExit("strict replay partial checkpoint differs")
        if payload.get("stop_reason") != "module requested exit" or \
                payload.get("final_marker") != "--":
            raise SystemExit("strict replay partial stop state differs")

        # A failure thrown from inside the runtime used to bypass the two
        # post-run partial-trace branches above. Force an immediate blocking
        # MEO WAIT/POLL mismatch and require the zero-consumption prefix too.
        mismatch_replay = root / "boundary-mismatch.txt"
        mismatch_trace = root / "boundary-mismatch-trace.json"
        mismatch_replay.write_text("POLL:QUIT\n", encoding="ascii")
        mismatch = subprocess.run(
            [
                str(args.executable),
                "--game", str(args.game),
                "--no-save",
                "--run-replay", str(mismatch_replay),
                "--trace-output", str(mismatch_trace),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        expected_mismatch = (
            "replay boundary mismatch at input 0: runtime requested WAIT "
            "but next input requires POLL")
        if mismatch.returncode == 0 or expected_mismatch not in mismatch.stderr:
            raise SystemExit(
                "strict runtime mismatch was not reported exactly:\n" +
                mismatch.stdout + mismatch.stderr)
        if not mismatch_trace.is_file():
            raise SystemExit("runtime boundary mismatch lost its partial trace")
        mismatch_payload = json.loads(
            mismatch_trace.read_text(encoding="utf-8"))
        if mismatch_payload.get("input") != {
                "total": 1, "consumed": 0, "remaining": 1,
                "implicit_quit_calls": 0}:
            raise SystemExit("runtime mismatch input summary differs")
        if mismatch_payload.get("boundaries", {}).get("wait") != 1 or \
                mismatch_payload.get("input_checkpoints") != []:
            raise SystemExit("runtime mismatch prefix evidence differs")

    print(
        "Strict replay failure trace: OK "
        "(post-run 1/2 and runtime mismatch 0/1 prefixes preserved)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
