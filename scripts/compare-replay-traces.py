#!/usr/bin/env python3
"""Compare normalized original/rewrite playthrough traces byte-exactly."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def load(path: Path) -> tuple[dict[str, object], str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise ValueError(f"unsupported replay trace schema: {path}")
    return data, hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("rewrite", type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--output", type=Path)
    group.add_argument("--verify-report", type=Path)
    args = parser.parse_args()
    try:
        original, original_sha = load(args.original)
        rewrite, rewrite_sha = load(args.rewrite)
        fields = {
            "input": original.get("input") == rewrite.get("input"),
            "boundaries": original.get("boundaries") == rewrite.get("boundaries"),
            "input_checkpoints": original.get("input_checkpoints")
            == rewrite.get("input_checkpoints"),
            "video": original.get("video") == rewrite.get("video"),
            "frames": original.get("frame_fnv1a64")
            == rewrite.get("frame_fnv1a64"),
            "audio": original.get("audio") == rewrite.get("audio"),
            "delay": original.get("delay_milliseconds")
            == rewrite.get("delay_milliseconds"),
            "final_state": original.get("state_fnv1a64")
            == rewrite.get("state_fnv1a64"),
            "final_mapz": original.get("mapz_fnv1a64")
            == rewrite.get("mapz_fnv1a64"),
            "stop": (
                original.get("stop_reason"),
                original.get("final_marker"),
            )
            == (rewrite.get("stop_reason"), rewrite.get("final_marker")),
            "transitions": original.get("transitions")
            == rewrite.get("transitions"),
        }
        verified = all(fields.values())
        report: dict[str, object] = {
            "schema_version": 1,
            "kind": "replay_comparison",
            "status": "verified" if verified else "mismatch",
            "original_sha256": original_sha,
            "rewrite_sha256": rewrite_sha,
            "checks": fields,
        }
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
        if args.verify_report:
            recorded = json.loads(args.verify_report.read_text(encoding="utf-8"))
            if recorded != report:
                raise ValueError("recorded comparison report differs from live trace comparison")
        if not verified:
            failed = [name for name, passed in fields.items() if not passed]
            raise ValueError(f"playthrough traces diverge: {failed}")
        print("replay comparison: VERIFIED (all frame/state/audio/transition checks match)")
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"replay comparison: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
