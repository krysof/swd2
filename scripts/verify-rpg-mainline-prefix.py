#!/usr/bin/env python3
"""Lock the released main story to Jianmu event 334's first dialogue."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"word at {offset:#x} is outside {len(data)} bytes")
    return data[offset] | (data[offset + 1] << 8)


def fnv1a64(data: bytes) -> str:
    digest = FNV_OFFSET
    for value in data:
        digest ^= value
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def map_area(image: bytes, location_offset: int) -> tuple[int, int]:
    location = u16(image, location_offset)
    area = u16(image, location + 26)
    count = u16(image, area + 4)
    if count == 0:
        raise ValueError(f"MAPZ location {location_offset} has no entities")
    return area, count


def map_field(image: bytes, location_offset: int, field: int,
              entity: int = 0) -> int:
    area, count = map_area(image, location_offset)
    if field >= 11 or entity >= count:
        raise ValueError("MAPZ field/entity index is outside the released area")
    return u16(image, area + 6 + field * count * 2 + entity * 2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("save", type=Path)
    parser.add_argument("mapz", type=Path)
    parser.add_argument("name", type=Path)
    args = parser.parse_args()
    try:
        trace = json.loads(args.trace.read_text(encoding="utf-8"))
        expected_transitions = [
            ("MEO.EXE", "--", "MT", True),
            ("RPG.EXE", "MT", "ED", True),
            ("DEMO.EXE", "ED", "--", True),
        ]
        # The released route enters FIG for the original ten encounters,
        # another 29 legal ONE2A training battles, fixed encounter 18, four
        # return/mine encounters, fixed Taotie encounter 16, then twenty-one
        # legal post-Taotie encounters through the sixth post-Xirang return
        # fight, the two overworld encounters on the road to the temple, and
        # fixed Jianmu encounter 14, the eastern TREE encounter, two released
        # random encounters on the first ascent, and the additional encounter
        # naturally triggered while descending into the treasure route.
        for index in range(73):
            expected_transitions.extend([
                ("RPG.EXE", "OM" if index == 0 else "OC", "IF", True),
                ("FIG.EXE", "IF", "OC", True),
            ])
        expected_transitions.append(("RPG.EXE", "OC", "--", True))
        transitions = [
            (item.get("module"), item.get("input"), item.get("output"),
             item.get("launched"))
            for item in trace.get("transitions", [])
        ]
        if transitions != expected_transitions:
            raise ValueError(f"mainline transitions differ: {transitions!r}")

        expected_values = {
            ("input", "total"): 12486,
            ("input", "consumed"): 12486,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 1579,
            ("boundaries", "poll"): 10944,
            ("boundaries", "text"): 944,
            ("video", "frames"): 36313,
            ("video", "direct_updates"): 5582,
            ("video", "fnv1a64"): "65bbb285ade91880",
            ("audio", "music_calls"): 335,
            ("audio", "voice_calls"): 696,
            ("audio", "stop_audio_calls"): 150,
            ("audio", "fnv1a64"): "302beef33037b672",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 1610213:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "6bc107b9cce898d1",
            "mapz_fnv1a64": "71248464a94ce452",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 36313 or frames[-1] != "2ac5faa0dd551a4c":
            raise ValueError("mainline final Jianmu-directory-252 frame differs")

        # Lock every deterministic boundary/frame/timeline/transition record
        # as canonical JSON instead of relying on sparse state samples.
        aggregate_sequences = {
            "input_checkpoints": (12486,
                "33ecd3959fe109b7185297d59aa4db2eb719aa1b47aeb8be7b5129f8f2ad9db2"),
            "frame_fnv1a64": (36313,
                "54263d6176252c45141a7428bba27a5d2dcc2e3ba9d8592c29f35769db81bd71"),
            "timeline": (128719,
                "3f69f6bc3e3be5c527c9285c840ffad7e562d88cc1be24783a5fcede625bda01"),
            "transitions": (150,
                "3cb02726a3fe61a96cd4527586b26e797cab6f48948b9bf0d1056b903647fe9b"),
        }
        for key, (count, expected_digest) in aggregate_sequences.items():
            sequence = trace.get(key)
            if not isinstance(sequence, list) or len(sequence) != count or \
                    canonical_sha256(sequence) != expected_digest:
                raise ValueError(f"mainline prefix complete {key} sequence differs")

        save = args.save.read_bytes()
        mapz = args.mapz.read_bytes()
        name = args.name.read_bytes()
        if hashlib.sha256(save).hexdigest() != \
                "4d525afef9ac3ad2bf11437b402efd049f010e06904970164ae2fc77fc94a842" or \
                hashlib.sha256(mapz).hexdigest() != \
                "1808df7017842eb51190ef989fc32daace139b75708b5287967df5c190d927b1" or \
                hashlib.sha256(name).hexdigest() != \
                "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba":
            raise ValueError("persisted mainline prefix artifact SHA-256 differs")
        if len(save) != 1350 or fnv1a64(save) != digests["state_fnv1a64"]:
            raise ValueError("persisted mainline SAVE differs from live state")
        if fnv1a64(mapz) != digests["mapz_fnv1a64"]:
            raise ValueError("persisted mainline MAPZ differs from live database")
        if len(name) != 514 or fnv1a64(name) != digests["name_fnv1a64"]:
            raise ValueError("persisted mainline NAME differs from live font")

        # The Taoist handoff adds flag 36 after all previously proven story
        # flags. The Jianmu gate redirects AREA1 entity 3, then event 322
        # moves the blocking root and event 324 installs its post-battle text.
        if u16(save, 0x4A2) != 0xE880:
            raise ValueError("village/Stronghold/river-demon story flags are not exact")
        if u16(save, 0x4A4) != 0x2202 or u16(save, 0x4A6) != 0x2808:
            raise ValueError(
                "freed-father/mayor/boat/tablet/xirang/Taoist flags are not exact")
        if u16(save, 0x4A8) != 0x8000:
            raise ValueError("ONE3A stone-lion story flag 48 was not set")
        if save[0x521] != 1:
            raise ValueError("far-side AREA1 travel flag three was not set")
        if u16(save, 0x424) != 252:
            raise ValueError("Jianmu continuation checkpoint is not directory 252")
        world_x = u16(save, 0x41B) + ((u16(save, 0x012) + 2) >> 1)
        world_y = u16(save, 0x41D) + ((u16(save, 0x02A) + 16) >> 3)
        if (world_x, world_y) != (39, 57):
            raise ValueError(f"Jianmu directory-252 position is {(world_x, world_y)!r}")
        if u16(save, 0x51C) != 0:
            raise ValueError("RPG did not consume and clear the post-battle entity continuation")
        if u16(save, 0x10) != 4 or u16(save, 0x104) != 4635:
            raise ValueError("Jianmu directory-252 party count or money differs")
        if u16(save, 0x49C) != 0x1300:
            raise ValueError("Jianmu directory-252 FIG random cursor differs")
        # The last Up enters directory 252 at (28,59), and MAP0 action 4011h
        # sets bit 0010h before calling entity one's event 334. Its initial
        # 1-up/11-right/1-up choreography produces (39,57); QUIT is consumed
        # by the first dialogue, before opcode 3/34 and opcode 58. This is a
        # diagnostic mid-event boundary, not proof that the story battle was
        # completed or a canonical persisted-resume point.
        if u16(save, 0x51A) != 0x0014:
            raise ValueError("Jianmu/Taotie MAP0 once-only trigger flags differ")
        expected_party = [
            (0x0000, 42, 134, 110, 110, 1, 84, 707, 717),
            (0x0000, 58, 138, 42, 42, 5, 91, 696, 706),
            (0x0000, 161, 161, 109, 140, 46, 46, 262, 847),
            (0x1000, 32, 140, 46, 46, 7, 84, 627, 702),
        ]
        for actor, expected in enumerate(expected_party):
            base = 0x106 + actor * 0x9F
            actual = (u16(save, base + 8), u16(save, base + 0x2D),
                      u16(save, base + 0x2F), u16(save, base + 0x35),
                      u16(save, base + 0x37), u16(save, base + 0x55),
                      u16(save, base + 0x57), u16(save, base + 0x39),
                      u16(save, base + 0x3B))
            if actual != expected:
                raise ValueError(
                    f"Jianmu directory-252 actor {actor} state is {actual!r}, "
                    f"expected {expected!r}")
        if [u16(save, 0x106 + actor * 0x9F + 0x31)
                for actor in range(4)] != [16, 16, 17, 16]:
            raise ValueError("post-guardian party levels differ")
        expected_inventory = [
            93, 205, 206, 100, 110, 206, 291, 333, 104, 119, 257,
        ] + [0] * 39
        actual_inventory = [u16(save, 0x382 + slot * 2) for slot in range(50)]
        if actual_inventory != expected_inventory:
            raise ValueError("Jianmu treasure inventory or stable compaction differs")

        header_size = u16(mapz, 8) * 16
        image = mapz[header_size:]
        if map_field(image, 10, 2) != 28192 or \
                map_field(image, 10, 3) != 0 or \
                map_field(image, 10, 9) != 74:
            raise ValueError("blocking-villager MAPZ mutation differs")
        if map_field(image, 50, 9) != 66:
            raise ValueError("village-chief persistent event redirect differs")
        if map_field(image, 14, 3, 39) != 3 or \
                map_field(image, 14, 9, 39) != 200:
            raise ValueError("Fire-Eyed Suanni was not persistently hidden")
        if map_field(image, 14, 3, 25) != 3 or \
                map_field(image, 14, 3, 38) != 3 or \
                map_field(image, 14, 3, 41) != 3:
            raise ValueError("Stronghold secret mechanism/wall mutation differs")
        if any(map_field(image, 12, 3, entity) != 3 for entity in range(10)):
            raise ValueError("SBOUT one-shot residents/guards were not all hidden")
        if any(map_field(image, 120, 9, entity) != 342
               for entity in (0, 1)):
            raise ValueError("ONE3A stone lions did not redirect to event 342")
        for location in (110, 122, 588):
            if map_field(image, location, 3, 19) != 3 or \
                    map_field(image, location, 9, 19) != 284 or \
                    map_field(image, location, 3, 20) != 3 or \
                    map_field(image, location, 9, 20) != 338:
                raise ValueError(
                    "river-demon event did not persist the aliased entity redirects")
        if map_field(image, 132, 3, 1) != 3 or \
                map_field(image, 132, 9, 1) != 350:
            raise ValueError("Taotie was not persistently hidden after victory")
        if map_field(image, 132, 9, 0) != 346:
            raise ValueError("Great Yu tablet did not persist event 278 -> 346")
        if map_field(image, 128, 9, 0) != 348 or \
                map_field(image, 130, 9, 0) != 348:
            raise ValueError("lake-bottom seal did not persist event 280 -> 348")
        if map_field(image, 176, 3, 0) != 3 or \
                map_field(image, 176, 9, 0) != 306:
            raise ValueError("Great Yu waterway gate was not persistently opened")
        if map_field(image, 198, 0, 0) != 49 or \
                map_field(image, 198, 9, 0) != 202:
            raise ValueError("event 316 chest did not persist its opened state")
        if map_field(image, 214, 9, 3) != 450:
            raise ValueError("Jianmu gate did not persist event 318 -> 450")
        if map_field(image, 216, 2, 0) != 42950:
            raise ValueError("Jianmu guardian did not move the blocking root")
        if map_field(image, 220, 9, 0) != 308:
            raise ValueError("Jianmu guardian did not persist event 322 -> 308")
        if map_field(image, 126, 3, 2) != 3:
            raise ValueError("Jianmu guardian did not hide directory 126 entity two")
        # S1ROB's six location records alias the same eleven-entity area.
        # Events 422/424/428 redirect the three stationary treasures to 404;
        # animated treasures 430..440 redirect to 364. Event 426 is the
        # released repeatable chest and deliberately retains its event word.
        expected_treasure_events = [
            310, 404, 404, 426, 404, 364, 364, 364, 364, 364, 364,
        ]
        for location in (236, 240, 244, 246, 250, 258):
            actual = [map_field(image, location, 9, entity)
                      for entity in range(11)]
            if actual != expected_treasure_events:
                raise ValueError(
                    f"Jianmu treasure events differ at location {location}: {actual!r}")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("mainline checkpoint did not stop at explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline prefix validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RPG mainline prefix validation: OK "
        "(village -> Fire-Eyed Suanni -> T2 -> stone lions -> river demon -> Taotie -> Great Yu tablet -> Xirang -> Taoist temple -> Jianmu treasure route -> directory 252)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
