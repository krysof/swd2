#!/usr/bin/env python3
"""Rebuild and verify the aggregate FIG exact-RGB checkpoint in memory."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


DIGEST = re.compile(r"^[0-9a-f]{64}$")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_digest(value: object, label: str) -> str:
    if not isinstance(value, str) or DIGEST.fullmatch(value) is None:
        raise ValueError(f"malformed digest: {label}")
    return value


def rgb_pairs(value: object):
    if isinstance(value, dict):
        rewrite = value.get("rewrite_rgb_sha256")
        original = value.get("original_rgb_sha256")
        if isinstance(rewrite, str) and isinstance(original, str):
            yield rewrite, original
        for child in value.values():
            yield from rgb_pairs(child)
    elif isinstance(value, list):
        for child in value:
            yield from rgb_pairs(child)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("checkpoint", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.checkpoint.read_text(encoding="utf-8"))
        sources = expected.get("sources")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "fig_rgb_checkpoint" or \
                expected.get("status") != "checkpoint_not_complete" or \
                not isinstance(expected.get("scope"), str) or \
                "not the final" not in expected["scope"] or \
                not isinstance(sources, list) or not sources:
            raise ValueError("unsupported FIG RGB checkpoint")

        discovered = sorted((root / "scripts").glob("fig-*-rgb-reference.json"))
        discovered = [
            path for path in discovered
            if list(rgb_pairs(json.loads(path.read_text(encoding="utf-8"))))
        ]
        listed = [source.get("path") for source in sources]
        wanted = [path.relative_to(root).as_posix() for path in discovered]
        if listed != wanted:
            raise ValueError("FIG RGB checkpoint source index is stale")

        total = 0
        exact_total = 0
        nonmatching_total = 0
        exact_digests: set[str] = set()
        for source, path in zip(sources, discovered):
            if not isinstance(source, dict) or \
                    sha256(path.read_bytes()) != require_digest(
                        source.get("sha256"), source.get("path", "source")):
                raise ValueError(f"FIG RGB reference hash differs: {path.name}")
            data = json.loads(path.read_text(encoding="utf-8"))
            pairs = list(rgb_pairs(data))
            if source.get("kind") != data.get("kind") or \
                    not isinstance(data.get("kind"), str) or \
                    not data["kind"].startswith("original_fig_") or \
                    not pairs or \
                    source.get("registered_rgb_pairs") != len(pairs):
                raise ValueError(f"FIG RGB reference boundary differs: {path.name}")
            exact = 0
            for index, (rewrite_value, original_value) in enumerate(pairs):
                rewrite = require_digest(
                    rewrite_value, f"{path.name}/{index}/rewrite_rgb")
                original = require_digest(
                    original_value, f"{path.name}/{index}/original_rgb")
                if rewrite == original:
                    exact += 1
                    exact_digests.add(rewrite)
            nonmatching = len(pairs) - exact
            if source.get("exact_rgb_pages") != exact or \
                    source.get("nonmatching_rgb_pairs") != nonmatching:
                raise ValueError(f"FIG RGB page count differs: {path.name}")
            total += len(pairs)
            exact_total += exact
            nonmatching_total += nonmatching

        if expected.get("source_count") != len(sources) or \
                expected.get("registered_rgb_pair_count") != total or \
                expected.get("exact_rgb_page_count") != exact_total or \
                expected.get("nonmatching_rgb_pair_count") != nonmatching_total or \
                expected.get("unique_exact_rgb_page_count") != len(exact_digests):
            raise ValueError("FIG RGB aggregate count differs")
        print(
            f"FIG RGB checkpoint: {len(sources)} references, "
            f"{exact_total} exact pages, {len(exact_digests)} unique RGB pages, "
            f"{nonmatching_total} nonmatching pairs not claimed")
        return 0
    except (OSError, ValueError, TypeError, KeyError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG RGB checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
