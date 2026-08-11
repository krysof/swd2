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

        discovered = sorted((root / "scripts").glob("*-rgb-reference.json"))
        discovered = [
            path for path in discovered
            if isinstance(json.loads(path.read_text(encoding="utf-8")).get(
                "matched_frames"), list)
        ]
        listed = [source.get("path") for source in sources]
        wanted = [path.relative_to(root).as_posix() for path in discovered]
        if listed != wanted:
            raise ValueError("FIG RGB checkpoint source index is stale")

        total = 0
        exact_total = 0
        exact_digests: set[str] = set()
        for source, path in zip(sources, discovered):
            if not isinstance(source, dict) or \
                    sha256(path.read_bytes()) != require_digest(
                        source.get("sha256"), source.get("path", "source")):
                raise ValueError(f"FIG RGB reference hash differs: {path.name}")
            data = json.loads(path.read_text(encoding="utf-8"))
            pages = data.get("matched_frames")
            if source.get("kind") != data.get("kind") or \
                    not isinstance(data.get("kind"), str) or \
                    not data["kind"].startswith("original_fig_") or \
                    not isinstance(pages, list) or not pages or \
                    source.get("registered_pages") != len(pages):
                raise ValueError(f"FIG RGB reference boundary differs: {path.name}")
            exact = 0
            for index, page in enumerate(pages):
                if not isinstance(page, dict):
                    raise ValueError(f"FIG RGB page is not an object: {path.name}")
                review = page.get("original_review_frame")
                if not isinstance(review, int) or review < 0:
                    raise ValueError(f"FIG RGB review frame differs: {path.name}")
                require_digest(
                    page.get("original_png_sha256"),
                    f"{path.name}/{index}/original_png")
                require_digest(
                    page.get("rewrite_indexed_sha256"),
                    f"{path.name}/{index}/rewrite_indexed")
                require_digest(
                    page.get("rewrite_palette_sha256"),
                    f"{path.name}/{index}/rewrite_palette")
                rewrite = require_digest(
                    page.get("rewrite_rgb_sha256"),
                    f"{path.name}/{index}/rewrite_rgb")
                original = require_digest(
                    page.get("original_rgb_sha256"),
                    f"{path.name}/{index}/original_rgb")
                if rewrite != original:
                    raise ValueError(f"FIG RGB mismatch remains: {path.name}/{index}")
                exact += 1
                exact_digests.add(rewrite)
            if source.get("exact_rgb_pages") != exact:
                raise ValueError(f"FIG RGB page count differs: {path.name}")
            total += len(pages)
            exact_total += exact

        if expected.get("source_count") != len(sources) or \
                expected.get("registered_page_count") != total or \
                expected.get("exact_rgb_page_count") != exact_total or \
                expected.get("unique_exact_rgb_page_count") != len(exact_digests):
            raise ValueError("FIG RGB aggregate count differs")
        print(
            f"FIG RGB checkpoint: {len(sources)} references, "
            f"{exact_total} exact pages, {len(exact_digests)} unique RGB pages")
        return 0
    except (OSError, ValueError, TypeError, KeyError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG RGB checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
