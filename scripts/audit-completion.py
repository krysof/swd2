#!/usr/bin/env python3
"""Validate SWD2's hard 100% completion gate."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "porting-status.json"
ALLOWED_STATUSES = {"pending", "in_progress", "verified"}


def fail(message: str) -> None:
    raise ValueError(message)


def load_and_validate() -> dict:
    try:
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read completion manifest: {error}")
    if data.get("schema_version") != 1:
        fail("completion manifest schema_version must be 1")
    gates = data.get("gates")
    if not isinstance(gates, list) or not gates:
        fail("completion manifest must contain at least one gate")
    commands = data.get("completion_commands")
    if not isinstance(commands, list) or not commands or not all(
        isinstance(command, str) and command.strip() for command in commands
    ):
        fail("completion_commands must be a non-empty string list")

    seen: set[str] = set()
    for index, gate in enumerate(gates):
        if not isinstance(gate, dict):
            fail(f"gate {index} is not an object")
        gate_id = gate.get("id")
        if not isinstance(gate_id, str) or not gate_id:
            fail(f"gate {index} has no id")
        if gate_id in seen:
            fail(f"duplicate gate id: {gate_id}")
        seen.add(gate_id)
        if not isinstance(gate.get("title"), str) or not gate["title"]:
            fail(f"gate {gate_id} has no title")
        status = gate.get("status")
        if status not in ALLOWED_STATUSES:
            fail(f"gate {gate_id} has invalid status: {status}")
        acceptance = gate.get("acceptance")
        if not isinstance(acceptance, list) or not acceptance or not all(
            isinstance(item, str) and item.strip() for item in acceptance
        ):
            fail(f"gate {gate_id} has no concrete acceptance criteria")
        evidence = gate.get("evidence")
        if not isinstance(evidence, list) or not all(
            isinstance(item, str) and item.strip() for item in evidence
        ):
            fail(f"gate {gate_id} evidence must be a string list")
        if status == "verified" and not evidence:
            fail(f"verified gate {gate_id} has no evidence")
        for relative in evidence:
            path = Path(relative)
            if path.is_absolute() or ".." in path.parts:
                fail(f"gate {gate_id} evidence must stay inside the repository: {relative}")
            if not (ROOT / path).exists():
                fail(f"gate {gate_id} evidence does not exist: {relative}")
        if status != "verified":
            remaining = gate.get("remaining")
            if not isinstance(remaining, list) or not remaining or not all(
                isinstance(item, str) and item.strip() for item in remaining
            ):
                fail(f"unfinished gate {gate_id} has no concrete remaining work")
    return data


def print_status(data: dict) -> list[dict]:
    gates = data["gates"]
    unfinished = [gate for gate in gates if gate["status"] != "verified"]
    for gate in gates:
        print(f"{gate['status']:11} {gate['id']}: {gate['title']}")
    print(f"\nVerified gates: {len(gates) - len(unfinished)}/{len(gates)}")
    print("Project completion: " + ("READY FOR FINAL CHECKS" if not unfinished else "NOT COMPLETE"))
    return unfinished


def run_completion_commands(data: dict) -> None:
    for command in data["completion_commands"]:
        print(f"\n>>> {command}", flush=True)
        completed = subprocess.run(command, cwd=ROOT, shell=True, check=False)
        if completed.returncode != 0:
            raise RuntimeError(
                f"completion command failed with exit code {completed.returncode}: {command}"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate", action="store_true", help="validate and report the manifest")
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="fail unless every gate is verified, then run every final command",
    )
    args = parser.parse_args()
    if args.validate and args.require_complete:
        parser.error("choose either --validate or --require-complete")
    try:
        data = load_and_validate()
        unfinished = print_status(data)
        if args.require_complete:
            if unfinished:
                print("\nUnfinished gates:", file=sys.stderr)
                for gate in unfinished:
                    print(f"- {gate['id']}: {gate['status']}", file=sys.stderr)
                return 1
            run_completion_commands(data)
            print("\nCOMPLETE=100%")
        return 0
    except (ValueError, RuntimeError) as error:
        print(f"completion audit error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

