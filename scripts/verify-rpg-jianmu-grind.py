#!/usr/bin/env python3
"""Validate the legal-prefix Jianmu natural-defeat continuation."""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


def fail(message: str) -> None:
    raise ValueError(message)


def fnv1a(data: bytes) -> str:
    value = FNV_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV_PRIME) & MASK64
    return f"{value:016x}"


def word(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        fail(f"word {offset:#x} is outside SAVE")
    return data[offset] | data[offset + 1] << 8


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: verify-rpg-jianmu-grind.py TRACE SAVE MAPZ NAME")
    trace_path, save_path, mapz_path, name_path = map(Path, sys.argv[1:])
    trace = json.loads(trace_path.read_text())
    save = save_path.read_bytes()
    mapz = mapz_path.read_bytes()
    name = name_path.read_bytes()

    if trace.get("input") != {
            "total": 133, "consumed": 133, "remaining": 0,
            "implicit_quit_calls": 0}:
        fail(f"strict input totals changed: {trace.get('input')!r}")
    boundaries = trace.get("boundaries", {})
    if (boundaries.get("wait"), boundaries.get("poll"),
            boundaries.get("text"), boundaries.get("frontend")) != (
                27, 106, 0, 2714):
        fail(f"WAIT/POLL/frontend totals changed: {boundaries!r}")
    video = trace.get("video", {})
    if (video.get("frames"), video.get("direct_updates"),
            video.get("fnv1a64")) != (684, 0, "db20dbf7c0291ab4"):
        fail(f"defeat video boundary changed: {video!r}")
    if trace.get("delay_milliseconds") != 43780:
        fail("defeat 70-Hz timing total changed")
    audio = trace.get("audio", {})
    if (audio.get("music_calls"), audio.get("voice_calls"),
            audio.get("stop_music_calls"), audio.get("stop_audio_calls"),
            audio.get("fnv1a64")) != (5, 29, 0, 3, "930e4f0ec14dddf6"):
        fail(f"defeat audio boundary changed: {audio!r}")

    aggregates = {
        "input_checkpoints": (133,
            "2055ca42125d40936ff9a489774a65f5984ce16b2d1a1698f642262a2adc82d3"),
        "frame_fnv1a64": (684,
            "bdd7875dc5003fc7a54a18b06474368b2a77fc59589b4bac69300bdcca8f1e0e"),
        "timeline": (3141,
            "dbb6c225da76dc448d2819e0e30d774255561f7f4365e4dd81b997154accf34b"),
        "transitions": (3,
            "598afd6b9586d8becfef29e2e44b8fb0bdb599af12ba7517715ccd61e5dccd35"),
    }
    for key, (count, digest) in aggregates.items():
        sequence = trace.get(key)
        if not isinstance(sequence, list) or len(sequence) != count or \
                canonical_sha256(sequence) != digest:
            fail(f"complete {key} sequence changed")
    transitions = trace["transitions"]
    actual_transitions = [
        (item.get("module"), item.get("input"), item.get("output"))
        for item in transitions]
    if actual_transitions != [
            ("RPG.EXE", "OC", "IF"), ("FIG.EXE", "IF", "OC"),
            ("RPG.EXE", "OC", "--")]:
        fail(f"module continuation changed: {actual_transitions!r}")
    if trace.get("stop_reason") != "module requested exit" or \
            trace.get("final_marker") != "--":
        fail("continuation did not stop through explicit frontend close")

    if (hashlib.sha256(save).hexdigest(),
            hashlib.sha256(mapz).hexdigest(),
            hashlib.sha256(name).hexdigest()) != (
                "225d53153d11113448c1c22aacd3f23bfef8754dff92845a142da77491254b51",
                "1808df7017842eb51190ef989fc32daace139b75708b5287967df5c190d927b1",
                "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba"):
        fail("persisted continuation artifact SHA-256 changed")
    if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
            trace.get("name_fnv1a64")) != (
                fnv1a(save), fnv1a(mapz), fnv1a(name)) or \
            (fnv1a(save), fnv1a(mapz), fnv1a(name)) != (
                "a06f38077d51b68e", "71248464a94ce452",
                "e3d2853e2676513b"):
        fail("persisted continuation digest changed")

    world_x = word(save, 0x41B) + ((word(save, 0x12) + 2) >> 1)
    world_y = word(save, 0x41D) + ((word(save, 0x2A) + 16) >> 3)
    if (word(save, 0x424), world_x, world_y, word(save, 0x408),
            word(save, 0x49C), word(save, 0x104)) != (
                252, 34, 57, 0x54, 0x144A, 4635):
        fail("Jianmu position/cursor/money changed")
    expected_party = [
        (0x3000, 0, 134, 1, 84, 110, 110, 16, 707, 717),
        (0x2000, 0, 138, 5, 91, 42, 42, 16, 696, 706),
        (0x2000, 0, 161, 46, 46, 109, 140, 17, 262, 847),
        (0x3000, 0, 140, 7, 84, 46, 46, 16, 627, 702),
    ]
    for actor, expected in enumerate(expected_party):
        base = 0x106 + actor * 0x9F
        actual = (
            word(save, base + 8), word(save, base + 0x2D),
            word(save, base + 0x2F), word(save, base + 0x55),
            word(save, base + 0x57), word(save, base + 0x35),
            word(save, base + 0x37), word(save, base + 0x31),
            word(save, base + 0x39), word(save, base + 0x3B))
        if actual != expected:
            fail(f"actor {actor} defeat state changed: {actual!r}")
    inventory = [word(save, 0x382 + slot * 2) for slot in range(50)]
    if inventory != [93, 205, 206, 100, 110, 206, 291, 333, 104, 119, 257] + [0] * 39:
        fail("defeat unexpectedly changed the legal-prefix inventory")

    print("Jianmu legal-prefix defeat checkpoint verified: 133 strict inputs")


if __name__ == "__main__":
    main()
