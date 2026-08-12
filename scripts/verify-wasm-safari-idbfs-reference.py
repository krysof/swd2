#!/usr/bin/env python3
"""Fail closed on the committed branded-Safari IDBFS checkpoint."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


DIGEST = re.compile(r"^[0-9a-f]{64}$")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        data = json.loads(args.reference.read_text(encoding="utf-8"))
        if data.get("schema_version") != 1 or \
                data.get("kind") != "wasm_branded_safari_idbfs_restart" or \
                data.get("status") != "verified":
            raise ValueError("Safari checkpoint schema/status differs")
        browser = data.get("browser")
        if not isinstance(browser, dict) or \
                browser.get("bundle_id") != "com.apple.Safari" or \
                not browser.get("version") or not browser.get("build"):
            raise ValueError("Safari checkpoint bundle identity differs")
        cycles = data.get("cycles")
        if not isinstance(cycles, list) or not cycles:
            raise ValueError("Safari checkpoint has no restart cycle")
        for index, cycle in enumerate(cycles):
            if not isinstance(cycle, dict) or cycle.get("cycle") != index or \
                    cycle.get("status") != "verified" or \
                    cycle.get("two_document_reload") is not True or \
                    cycle.get("exact_probe_bytes_restored") is not True or \
                    cycle.get("cleanup_sync") is not True:
                raise ValueError(f"Safari checkpoint cycle {index} differs")
            user_agent = cycle.get("user_agent")
            if not isinstance(user_agent, str) or "Version/" not in user_agent or \
                    "Safari/" not in user_agent or "Chrome/" in user_agent:
                raise ValueError(f"Safari checkpoint cycle {index} UA differs")
        assets = data.get("assets")
        if not isinstance(assets, dict) or \
                set(assets) != {"index.html", "index.js", "index.wasm", "index.data"} or \
                any(not isinstance(value, str) or DIGEST.fullmatch(value) is None
                    for value in assets.values()):
            raise ValueError("Safari checkpoint asset digests differ")
        limitation = data.get("limitation")
        if not isinstance(limitation, str) or "not physical iOS" not in limitation:
            raise ValueError("Safari checkpoint overstates its platform scope")
        print(
            "WASM branded Safari IDBFS checkpoint: "
            f"{len(cycles)} verified two-document restart cycle(s), "
            f"Safari {browser['version']}")
        return 0
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"WASM branded Safari IDBFS checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
