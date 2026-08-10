#!/usr/bin/env python3
"""Prove every modern FIG player-effect visual handler has original RGB evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


REFERENCE_PROGRAM_SHA256 = (
    "c4e0f0d04d25b70f80ab3d3490a81ef78c375193ccac70dad5b09c186c758876"
)
ALTERNATE_EVIDENCE = {
    0x33: ("fig-player-damage-rgb-reference.json", 52),
    0x61: ("fig-player-dispel-rgb-reference.json", 71),
    0x62: ("fig-player-barrier-rgb-reference.json", 64),
    0x63: ("fig-flagged-medium-rgb-reference.json", 86),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed evidence digest {label}")
    return value


def mz_image(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:2] != b"MZ" or len(data) < 0x1C:
        raise ValueError(f"not a DOS MZ executable: {path}")
    header_bytes = int.from_bytes(data[8:10], "little") * 16
    if header_bytes < 0x1C or header_bytes > len(data):
        raise ValueError(f"invalid DOS MZ header: {path}")
    return data[header_bytes:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    try:
        source = args.source.read_text(encoding="utf-8")
        switch = source[source.index(
            "std::span<const std::uint16_t> fig_effect_resource_sequence"):]
        handlers = {
            int(value, 16) for value in re.findall(
                r"case 0x([0-9a-f]+): return effect_[0-9a-f]+;", switch)
        }
        expected_handlers = {
            *range(0x32, 0x3B), *range(0x40, 0x47),
            *range(0x48, 0x66),
        }
        if handlers != expected_handlers:
            raise ValueError(
                "player-effect visual handler domain changed: "
                f"{[hex(value) for value in sorted(handlers)]}")

        fig_path = args.game / "FIG.EXE"
        if sha256(fig_path.read_bytes()) != REFERENCE_PROGRAM_SHA256:
            raise ValueError("FIG.EXE differs from the effect-coverage reference")
        fig = mz_image(fig_path)
        if fig[:1] != b"\xB8" or fig[3:5] != b"\x8E\xD8":
            raise ValueError("FIG.EXE data prologue changed")
        table = int.from_bytes(fig[1:3], "little") * 16 + 0x1DCC

        references: dict[int, tuple[int, Path, dict[str, object]]] = {}
        for path in sorted(
                args.evidence.glob("fig-player-effect*-rgb-reference.json")):
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("kind") != "original_fig_player_effect":
                continue
            effect = payload.get("effect_code")
            ability = payload.get("ability_id")
            if not isinstance(effect, int) or not isinstance(ability, int):
                raise ValueError(f"malformed player-effect reference: {path}")
            if effect in references:
                raise ValueError(f"duplicate player-effect reference: {effect:02x}h")
            references[effect] = (ability, path, payload)

        regular_handlers = handlers - ALTERNATE_EVIDENCE.keys()
        if references.keys() != regular_handlers:
            missing = regular_handlers - references.keys()
            extra = references.keys() - regular_handlers
            raise ValueError(
                "player-effect RGB evidence domain changed: "
                f"missing={[hex(value) for value in sorted(missing)]}, "
                f"extra={[hex(value) for value in sorted(extra)]}")

        for effect, (filename, ability) in ALTERNATE_EVIDENCE.items():
            path = args.evidence / filename
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("effect_code") != effect or \
                    payload.get("ability_id") != ability:
                raise ValueError(f"alternate evidence changed for effect {effect:02x}h")
            references[effect] = (ability, path, payload)

        matched_pages = 0
        for effect in sorted(handlers):
            ability, path, payload = references[effect]
            record = table + ability * 20
            if record + 20 > len(fig) or \
                    int.from_bytes(fig[record + 14:record + 16], "little") != effect:
                raise ValueError(
                    f"ability {ability} no longer dispatches effect {effect:02x}h")
            if payload.get("reference_program_sha256") != \
                    REFERENCE_PROGRAM_SHA256:
                raise ValueError(f"reference program changed: {path}")
            digest(payload.get("capture_video_sha256"), f"{path}/video")
            digest(payload.get("capture_manifest_sha256"), f"{path}/manifest")
            pages = payload.get("matched_frames")
            if not isinstance(pages, list) or not pages:
                raise ValueError(f"effect reference has no matched pages: {path}")
            for index, page in enumerate(pages):
                if not isinstance(page, dict) or \
                        page.get("original_rgb_sha256") != \
                        page.get("rewrite_rgb_sha256"):
                    raise ValueError(
                        f"unmatched effect RGB page: {path}/{index}")
                digest(page.get("original_rgb_sha256"), f"{path}/{index}/rgb")
            matched_pages += len(pages)

        if len(references) != 46 or matched_pages != 1688:
            raise ValueError(
                f"effect coverage totals changed: {len(references)} handlers, "
                f"{matched_pages} pages")
        print(
            "FIG player-effect coverage: all 46 visual handlers have "
            "original RGB evidence across 1,688 registered stable pages"
        )
        return 0
    except (OSError, ValueError, KeyError, IndexError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG player-effect coverage: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
