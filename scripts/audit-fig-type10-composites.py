#!/usr/bin/env python3
"""Prove every shipped type-10/6Bh composite item has original RGB evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED = {
    195: (0x31, 0x3c, "fig-player-buff-expiry-item-composite-media"),
    201: (0x3b, 0x31, "fig-player-buff-expiry-item-composite-reversed-media"),
    202: (0x39, 0x44, "fig-player-buff-expiry-item-composite-targetless-damage"),
    209: (0x3b, 0x3c, "fig-player-buff-expiry-item-composite-af-b0-media"),
    215: (0x43, 0x45, "fig-player-buff-expiry-item-composite-missing-af-b0"),
    219: (0x66, 0x69, "fig-player-buff-expiry-item-composite-support"),
    220: (0x60, 0x46, "fig-player-buff-expiry-item-composite-status-damage"),
    225: (0x43, 0x36, "fig-player-buff-expiry-item-composite-missing-media"),
    230: (0x38, 0x3a, "fig-player-buff-expiry-item-composite-damage"),
    232: (0x4c, 0x5f, "fig-player-buff-expiry-item-composite-targetless-status"),
    236: (0x41, 0x37, "fig-player-buff-expiry-item-composite-damage-missing-af"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def archive_records(executable: bytes) -> list[bytes]:
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError("ITEM.EXE is not an MZ archive")
    header = int.from_bytes(executable[8:10], "little") * 16
    image = executable[header:]
    directory_bytes = int.from_bytes(image[2:4], "little")
    sentinel = int.from_bytes(image[:2], "little")
    if directory_bytes < 4 or directory_bytes > len(image) or \
            directory_bytes % 2:
        raise ValueError("ITEM.EXE directory is malformed")
    offsets = [int.from_bytes(image[pos:pos + 2], "little")
               for pos in range(0, directory_bytes, 2)]
    records = []
    for start in offsets:
        finish = min((offset for offset in offsets if offset > start),
                     default=sentinel)
        if start > finish or finish > len(image):
            raise ValueError("ITEM.EXE record bounds are malformed")
        records.append(image[start:finish] if start < finish else b"")
    return records


def valid_digest(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        char in "0123456789abcdef" for char in value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    args = parser.parse_args()
    try:
        item_exe = (args.game / "ITEM.EXE").read_bytes()
        records = archive_records(item_exe)
        shipped: dict[int, tuple[int, int]] = {}
        # ITEM ids use archive directory index id+2.
        for item_id in range(len(records) - 2):
            record = records[item_id + 2]
            if len(record) >= 11 and \
                    int.from_bytes(record[0:2], "little") == 0x10 and \
                    int.from_bytes(record[7:9], "little") == 0x6b:
                shipped[item_id] = (record[9], record[10])
        expected_pairs = {
            item_id: (entry[0], entry[1])
            for item_id, entry in EXPECTED.items()
        }
        if shipped != expected_pairs:
            raise ValueError(
                f"shipped type-10/6Bh domain differs: {shipped!r}")

        exact_pages = 0
        crop_pages = 0
        for item_id, (first, second, stem) in EXPECTED.items():
            reference_path = root / "scripts" / f"{stem}-rgb-reference.json"
            verifier_path = root / "scripts" / f"verify-{stem}.py"
            if not reference_path.is_file() or not verifier_path.is_file():
                raise ValueError(f"item {item_id} evidence file is absent")
            reference = json.loads(reference_path.read_text(encoding="utf-8"))
            if reference.get("schema_version") != 1 or \
                    reference.get("status") != "partial_exact_rgb_checkpoint" or \
                    reference.get("item_id") != item_id or \
                    reference.get("item_effect_code") != 0x6b or \
                    reference.get("nested_effects") != [first, second]:
                raise ValueError(f"item {item_id} reference metadata differs")
            pages = reference.get("matched_frames")
            crops = reference.get("cropped_frames", [])
            if not isinstance(pages, list) or not pages or \
                    not isinstance(crops, list):
                raise ValueError(f"item {item_id} has no exact RGB evidence")
            for page in pages:
                rewrite = page.get("rewrite_rgb_sha256")
                original = page.get("original_rgb_sha256")
                if not valid_digest(rewrite) or rewrite != original:
                    raise ValueError(
                        f"item {item_id} contains a non-exact full page")
            for page in crops:
                rewrite = page.get("rewrite_crop_rgb_sha256")
                original = page.get("original_crop_rgb_sha256")
                if not valid_digest(rewrite) or rewrite != original:
                    raise ValueError(
                        f"item {item_id} contains a non-exact crop")
            for path_key, hash_key in (
                    ("capture_autotype", "capture_autotype_sha256"),
                    ("replay_input", "replay_input_sha256")):
                evidence = root / "scripts" / reference[path_key]
                if not evidence.is_file() or \
                        sha256(evidence.read_bytes()) != reference[hash_key]:
                    raise ValueError(
                        f"item {item_id} {path_key} evidence differs")
            if not valid_digest(reference.get("capture_video_sha256")) or \
                    not valid_digest(reference.get("capture_manifest_sha256")):
                raise ValueError(f"item {item_id} capture digest is malformed")
            exact_pages += len(pages)
            crop_pages += len(crops)

        print(
            "FIG type-10 composite coverage: "
            f"{len(EXPECTED)}/{len(EXPECTED)} shipped items, "
            f"{exact_pages} exact full RGB pages, {crop_pages} exact crops")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG type-10 composite coverage: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
