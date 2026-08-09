#!/usr/bin/env python3
"""Verify hashed completion evidence for one SWD2 release gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

REQUIRED_ROLES = {
    "pixel_diffs": {"baseline", "rewrite_output", "diff_report"},
    "playthrough": {"input", "original_trace", "rewrite_trace", "comparison"},
    "long_run": {"native_log", "browser_log", "matrix_report"},
}


def fail(message: str) -> None:
    raise ValueError(message)


def inside_repository(relative: object) -> Path:
    if not isinstance(relative, str) or not relative:
        fail("artifact path must be a non-empty string")
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        fail(f"artifact must stay inside the repository: {relative}")
    return ROOT / path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("pixel_diffs", "playthrough", "long_run"))
    args = parser.parse_args()
    manifest_path = ROOT / "verification" / args.kind / "manifest.json"
    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        if data.get("schema_version") != 1 or data.get("kind") != args.kind:
            fail("manifest schema/kind does not match the requested gate")
        if data.get("status") != "verified":
            fail("manifest status is not verified")
        artifacts = data.get("artifacts")
        if not isinstance(artifacts, list) or not artifacts:
            fail("manifest has no hashed artifacts")
        seen: set[str] = set()
        roles: set[str] = set()
        for artifact in artifacts:
            if not isinstance(artifact, dict):
                fail("artifact entry is not an object")
            relative = artifact.get("path")
            path = inside_repository(relative)
            if relative in seen:
                fail(f"duplicate artifact: {relative}")
            seen.add(relative)
            role = artifact.get("role")
            if not isinstance(role, str) or not role:
                fail(f"artifact has no evidence role: {relative}")
            roles.add(role)
            expected = artifact.get("sha256")
            if not isinstance(expected, str) or len(expected) != 64:
                fail(f"artifact has no full SHA-256: {relative}")
            if not path.is_file():
                fail(f"artifact is missing: {relative}")
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != expected.lower():
                fail(f"artifact hash differs: {relative}")
        missing_roles = sorted(REQUIRED_ROLES[args.kind] - roles)
        if missing_roles:
            fail(f"manifest is missing required evidence roles: {missing_roles}")
        print(f"{args.kind} evidence: OK ({len(artifacts)} hashed artifacts)")
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"{args.kind} evidence: NOT VERIFIED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
