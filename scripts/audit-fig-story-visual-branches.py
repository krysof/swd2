#!/usr/bin/env python3
"""Prove every directory-specific FIG visual branch has RGB evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


PROGRAM_SHA256 = (
    "c4e0f0d04d25b70f80ab3d3490a81ef78c375193ccac70dad5b09c186c758876"
)
REFERENCES = {
    0x30: ("fig-story-30-rgb-reference.json",
           "original_fig_story_30_opening", 6),
    0x42: ("fig-story-42-rgb-reference.json",
           "original_fig_story_42_hit_reaction", 9),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed digest {label}")
    return value


def load_image(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1C:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid MZ header")
    return executable[header:]


def require_bytes(image: bytes, offset: int, expected_hex: str,
                  label: str) -> None:
    expected = bytes.fromhex(expected_hex)
    if image[offset:offset + len(expected)] != expected:
        raise ValueError(f"FIG {label} machine code differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    parser.add_argument("source", type=Path, nargs="?",
                        default=root / "src" / "battle_module.cpp")
    parser.add_argument("evidence", type=Path, nargs="?",
                        default=root / "scripts")
    args = parser.parse_args()
    try:
        executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(executable) != PROGRAM_SHA256:
            raise ValueError("FIG.EXE differs from the story-branch reference")
        image = load_image(executable)

        # FIG setup checks 42h first, preloads CD523 and returns.  It then
        # checks 30h and otherwise returns without a directory-specific page.
        require_bytes(
            image, 0x32AA,
            "833ea004427519a13946050040a33143b80b02bb9d2ae846e9e820fde8b432c3"
            "833ea004307403e9b100",
            "story setup dispatcher")
        # Normal hit rendering has the sole directory-specific reaction: 42h
        # selects the preloaded +4000 segment, all others use sprite frame 1.
        require_bytes(
            image, 0x2E30,
            "833ea00442750ba13946050040a33143eb0bc70623430100e8404b",
            "story hit-reaction dispatcher")
        # Each of the six directory-30 pages is presented for exactly 3 ticks.
        require_bytes(
            image, 0x3386,
            "e8b032e84a3bb80300e8ab04e840fac3",
            "story page wait")

        source = args.source.read_text(encoding="utf-8")
        literals = {
            int(value, 16) for value in re.findall(
                r"encounter_directory_offset\s*(?:==|!=)\s*0x([0-9a-fA-F]+)",
                source)
        }
        if literals != REFERENCES.keys():
            raise ValueError(
                "modern directory-specific visual domain changed: " +
                repr([hex(value) for value in sorted(literals)]))

        pages = 0
        for directory, (filename, kind, count) in REFERENCES.items():
            path = args.evidence / filename
            payload = json.loads(path.read_text(encoding="utf-8"))
            frames = payload.get("frames")
            if payload.get("schema_version") != 1 or \
                    payload.get("kind") != kind or \
                    payload.get("formation_directory_offset") != directory or \
                    payload.get("reference_program_sha256") != PROGRAM_SHA256 or \
                    not isinstance(frames, list) or len(frames) != count:
                raise ValueError(f"unsupported story evidence: {path.name}")
            for name in ("capture_video_sha256", "capture_manifest_sha256"):
                digest(payload.get(name), f"{path.name}/{name}")
            for index, frame in enumerate(frames):
                original = digest(
                    frame.get("original_rgb_sha256"),
                    f"{path.name}/frame{index}/original")
                rewrite = digest(
                    frame.get("rewrite_rgb_sha256"),
                    f"{path.name}/frame{index}/rewrite")
                if original != rewrite:
                    raise ValueError(
                        f"non-exact story RGB page: {path.name}/{index}")
            pages += len(frames)

        print(
            "FIG story visual branches: 30h/42h are the complete "
            f"directory-specific domain, {pages} exact original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG story visual branches: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
