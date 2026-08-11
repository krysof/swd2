#!/usr/bin/env python3
"""Build the aggregate, non-final FIG exact-RGB coverage checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument(
        "output", type=Path,
        nargs="?",
        default=root / "verification" / "pixel_diffs" /
        "checkpoint-2026-08-11-fig-rgb.json",
    )
    args = parser.parse_args()
    try:
        sources: list[dict[str, object]] = []
        exact_digests: set[str] = set()
        registered_pages = 0
        for path in sorted((root / "scripts").glob("*-rgb-reference.json")):
            raw = path.read_bytes()
            data = json.loads(raw)
            pages = data.get("matched_frames")
            if not isinstance(pages, list):
                continue
            exact = 0
            for page in pages:
                if not isinstance(page, dict):
                    raise ValueError(f"matched frame is not an object: {path.name}")
                rewrite = page.get("rewrite_rgb_sha256")
                original = page.get("original_rgb_sha256")
                if rewrite == original:
                    exact += 1
                    if isinstance(rewrite, str):
                        exact_digests.add(rewrite)
            sources.append({
                "path": path.relative_to(root).as_posix(),
                "sha256": sha256(raw),
                "kind": data.get("kind"),
                "registered_pages": len(pages),
                "exact_rgb_pages": exact,
            })
            registered_pages += len(pages)

        output = {
            "schema_version": 1,
            "kind": "fig_rgb_checkpoint",
            "status": "checkpoint_not_complete",
            "scope": (
                "Aggregate index of every committed FIG reference containing "
                "matched_frames; this is not the final all-scene pixel_diffs "
                "completion manifest."
            ),
            "source_count": len(sources),
            "registered_page_count": registered_pages,
            "exact_rgb_page_count": sum(
                int(source["exact_rgb_pages"]) for source in sources),
            "unique_exact_rgb_page_count": len(exact_digests),
            "sources": sources,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(output, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(
            "FIG RGB checkpoint: "
            f"{output['source_count']} references, "
            f"{output['exact_rgb_page_count']} exact pages, "
            f"{output['unique_exact_rgb_page_count']} unique RGB pages")
        return 0
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"FIG RGB checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
