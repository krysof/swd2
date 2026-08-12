#!/usr/bin/env python3
"""Build the aggregate, non-final FIG exact-RGB coverage checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rgb_pairs(value: object):
    if isinstance(value, dict):
        rewrite = value.get("rewrite_rgb_sha256")
        original = value.get("original_rgb_sha256")
        rewrite_crop = value.get("rewrite_crop_rgb_sha256")
        original_crop = value.get("original_crop_rgb_sha256")
        has_crop = isinstance(rewrite_crop, str) and \
            isinstance(original_crop, str)
        if isinstance(rewrite, str) and isinstance(original, str) and \
                (not has_crop or rewrite == original):
            yield "page", rewrite, original
        if has_crop:
            yield "crop", rewrite_crop, original_crop
        for child in value.values():
            yield from rgb_pairs(child)
    elif isinstance(value, list):
        for child in value:
            yield from rgb_pairs(child)


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
        exact_page_digests: set[str] = set()
        exact_crop_digests: set[str] = set()
        registered_pairs = 0
        nonmatching_pairs = 0
        for path in sorted((root / "scripts").glob("fig-*-rgb-reference.json")):
            raw = path.read_bytes()
            data = json.loads(raw)
            pairs = list(rgb_pairs(data))
            if not pairs:
                continue
            exact_pages = 0
            exact_crops = 0
            for region, rewrite, original in pairs:
                if rewrite == original:
                    if region == "page":
                        exact_pages += 1
                        exact_page_digests.add(rewrite)
                    else:
                        exact_crops += 1
                        exact_crop_digests.add(rewrite)
            sources.append({
                "path": path.relative_to(root).as_posix(),
                "sha256": sha256(raw),
                "kind": data.get("kind"),
                "registered_rgb_pairs": len(pairs),
                "exact_rgb_pages": exact_pages,
                "exact_rgb_crops": exact_crops,
                "nonmatching_rgb_pairs":
                    len(pairs) - exact_pages - exact_crops,
            })
            registered_pairs += len(pairs)
            nonmatching_pairs += len(pairs) - exact_pages - exact_crops

        output = {
            "schema_version": 2,
            "kind": "fig_rgb_checkpoint",
            "status": "checkpoint_not_complete",
            "scope": (
                "Aggregate index of every committed FIG reference containing "
                "registered rewrite/original RGB page or crop digest pairs; "
                "this is not the final all-scene pixel_diffs "
                "completion manifest."
            ),
            "source_count": len(sources),
            "registered_rgb_pair_count": registered_pairs,
            "exact_rgb_page_count": sum(
                int(source["exact_rgb_pages"]) for source in sources),
            "exact_rgb_crop_count": sum(
                int(source["exact_rgb_crops"]) for source in sources),
            "nonmatching_rgb_pair_count": nonmatching_pairs,
            "unique_exact_rgb_page_count": len(exact_page_digests),
            "unique_exact_rgb_crop_count": len(exact_crop_digests),
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
            f"{output['exact_rgb_crop_count']} exact crops, "
            f"{output['unique_exact_rgb_page_count']} unique RGB pages, "
            f"{output['nonmatching_rgb_pair_count']} nonmatching pairs not claimed")
        return 0
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"FIG RGB checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
