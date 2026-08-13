#!/usr/bin/env python3
"""Validate the seven-fight, release-rule Jianmu training continuation."""

from __future__ import annotations

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


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: verify-rpg-jianmu-grind.py TRACE SAVE MAPZ NAME")
    trace_path, save_path, mapz_path, name_path = map(Path, sys.argv[1:])
    trace = json.loads(trace_path.read_text())
    save = save_path.read_bytes()
    mapz = mapz_path.read_bytes()
    name = name_path.read_bytes()

    expected_input = {
        "total": 1490,
        "consumed": 1490,
        "remaining": 0,
        "implicit_quit_calls": 0,
    }
    if trace.get("input") != expected_input:
        fail(f"strict input totals changed: {trace.get('input')!r}")
    boundaries = trace.get("boundaries", {})
    if boundaries.get("wait") != 631 or boundaries.get("poll") != 859:
        fail(f"WAIT/POLL boundary totals changed: {boundaries!r}")
    if boundaries.get("text") != 0:
        fail("training continuation unexpectedly consumed TEXT input")

    video = trace.get("video", {})
    if video.get("frames") != 7108 or video.get("direct_updates") != 0:
        fail(f"training video boundary changed: {video!r}")
    if trace.get("delay_milliseconds") != 437716:
        fail("training 70 Hz/event timing total changed")
    audio = trace.get("audio", {})
    if (audio.get("music_calls"), audio.get("voice_calls"),
            audio.get("stop_audio_calls")) != (33, 252, 15):
        fail(f"training audio call boundary changed: {audio!r}")

    transitions = trace.get("transitions", [])
    if len(transitions) != 15:
        fail(f"expected fourteen battle transitions plus final RPG, got {len(transitions)}")
    for battle in range(7):
        rpg = transitions[battle * 2]
        fig = transitions[battle * 2 + 1]
        if (rpg.get("module"), rpg.get("input"), rpg.get("output")) != (
                "RPG.EXE", "OC", "IF"):
            fail(f"training RPG transition {battle + 1} changed: {rpg!r}")
        if (fig.get("module"), fig.get("input"), fig.get("output")) != (
                "FIG.EXE", "IF", "OC"):
            fail(f"training FIG transition {battle + 1} changed: {fig!r}")
    if (transitions[-1].get("module"), transitions[-1].get("input"),
            transitions[-1].get("output")) != ("RPG.EXE", "OC", "--"):
        fail(f"final resumed RPG transition changed: {transitions[-1]!r}")
    if trace.get("stop_reason") != "module requested exit" or \
            trace.get("final_marker") != "--":
        fail("training continuation did not stop through explicit RPG quit")

    if trace.get("state_fnv1a64") != fnv1a(save):
        fail("trace SAVE digest does not match checkpoint")
    if trace.get("mapz_fnv1a64") != fnv1a(mapz):
        fail("trace MAPZ digest does not match checkpoint")
    if trace.get("name_fnv1a64") != fnv1a(name):
        fail("trace NAME digest does not match checkpoint")
    if fnv1a(save) != "909e2b3853abdbf5":
        fail("Jianmu training SAVE digest changed")
    if fnv1a(mapz) != "71248464a94ce452":
        fail("Jianmu training unexpectedly mutated MAPZ")
    if fnv1a(name) != "e3d2853e2676513b":
        fail("Jianmu training unexpectedly mutated NAME")

    if word(save, 0x424) != 252:
        fail("training continuation is not in Jianmu directory 252")
    world_x = word(save, 0x41B) + ((word(save, 0x12) + 2) >> 1)
    world_y = word(save, 0x41D) + ((word(save, 0x2A) + 16) >> 3)
    if (world_x, world_y) != (27, 57):
        fail(f"unexpected Jianmu training position {(world_x, world_y)}")
    if word(save, 0x408) != 0x54 or word(save, 0x49C) != 0x1B94:
        fail("Jianmu map selector or release random cursor changed")
    if word(save, 0x104) != 5601:
        fail("seven released encounters did not grant exactly 966 money")

    expected_party = [
        # status, hp/max, ap/max, sp/max, level, residual exp, next threshold
        (0x1000, 25, 156, 2, 94, 60, 122, 17, 627, 843),
        (0x1000, 9, 150, 3, 99, 35, 46, 17, 627, 842),
        (0x0000, 172, 172, 50, 50, 151, 151, 18, 52, 978),
        (0x1000, 6, 153, 3, 94, 38, 50, 17, 562, 838),
    ]
    for actor, expected in enumerate(expected_party):
        base = 0x106 + actor * 0x9F
        actual = (
            word(save, base + 8),
            word(save, base + 0x2D), word(save, base + 0x2F),
            word(save, base + 0x55), word(save, base + 0x57),
            word(save, base + 0x35), word(save, base + 0x37),
            word(save, base + 0x31), word(save, base + 0x39),
            word(save, base + 0x3B),
        )
        if actual != expected:
            fail(f"actor {actor} post-training state changed: {actual!r}")
    actor_two_abilities = list(save[0x106 + 2 * 0x9F + 0x6D:
                                    0x106 + 2 * 0x9F + 0x71])
    if actor_two_abilities != [15, 16, 18, 0]:
        fail(f"level-18 learned ability evidence changed: {actor_two_abilities!r}")

    inventory = [word(save, 0x382 + slot * 2) for slot in range(50)]
    expected_inventory = [93, 110, 206, 291, 104, 119, 257] + [0] * 43
    if inventory != expected_inventory:
        fail(f"released item consumption/compaction changed: {inventory!r}")

    print(
        "Jianmu training checkpoint verified: 7 ORC-356 wins, "
        "actor 2 level 18, 1,490 strict inputs")


if __name__ == "__main__":
    main()
