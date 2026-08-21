#!/usr/bin/env python3
"""Lock the continuous legal route from a new game through ending opcode 52."""

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
        # The released route reaches every boundary without a checkpoint
        # reload. The FIR3, pot-world, false-immortal and item-89 chains
        # include natural encounters plus fixed 8016h/8018h/8030h/801ah/
        # 801ch/801eh/8022h/8024h/4040h battles, then the ZF/T9 route and
        # EAST16 event 54/8034h, WEST12 event 46/802eh, SOUT56 event
        # 58/8038h and NORT89 event 322/8032h; every one is driven by released
        # commands and RNG. FIG's
        # released defeat epilogue returns OC, so the late trace deliberately
        # records those legal loss paths rather than editing HP. After the
        # four-treasure gate the same process returns through the released
        # world graph, uses script-produced travel items 278/281, and executes
        # ending events 448/452/454/456 plus their fixed FIG encounters.
        for index in range(314):
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
            ("input", "total"): 57837,
            ("input", "consumed"): 57837,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 2410,
            ("boundaries", "poll"): 55457,
            ("boundaries", "text"): 6666,
            ("boundaries", "frontend"): 180745,
            ("video", "frames"): 115481,
            ("video", "direct_updates"): 21023,
            ("video", "fnv1a64"): "02d3f3e6e2dcab8a",
            ("audio", "music_calls"): 1376,
            ("audio", "voice_calls"): 973,
            ("audio", "stop_music_calls"): 34,
            ("audio", "stop_audio_calls"): 632,
            ("audio", "fnv1a64"): "e407aa5d9e590296",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 4496458:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "e3fd6a1f5031b981",
            "mapz_fnv1a64": "33ce5f05411d9b49",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 115481 or frames[-1] != "1508f76e944a53db":
            raise ValueError("mainline completed-ending frame differs")
        expected_final_flags = [
            0, 1, 2, 4, 8, 18, 22, 30, 32, 33, 34, 35, 36, 37,
            39, 41, 44, 48, 50, 54, 56, 68, 70, 72, 90,
        ]
        expected_final_inventory = [
            262, 206, 110, 206, 291, 104, 119, 257, 83, 89, 278, 250, 281,
            316,
        ] + [0] * 36
        final_state = trace.get("final_state", {})
        if (final_state.get("map_location"), final_state.get("world_x"),
                final_state.get("world_y"), final_state.get("actor_direction"),
                final_state.get("battle_auxiliary")) != (220, 38, 16, 3, 0):
            raise ValueError("mainline final-state location/battle boundary differs")
        if final_state.get("story_flags") != expected_final_flags:
            raise ValueError("mainline final-state story flags differ")
        if final_state.get("inventory") != expected_final_inventory:
            raise ValueError("mainline final-state ending inventory differs")

        # Hash the complete deterministic sequences rather than sampling a
        # hand-maintained subset.  This locks every consumed input boundary,
        # submitted frame, audio/video timeline event and module transition.
        aggregate_sequences = {
            "input_checkpoints": (57837,
                "4b7a06ca6f8c0c1755d9cabacf01e64a0120f5875dad5d8e70714898df5616dd"),
            "frame_fnv1a64": (115481,
                "eac993d9cf1fb1a33f30331c9494eb1cca694a53c7d1082ca198e0d1ddc664f9"),
            "timeline": (381286,
                "d4dcde10fdcb27a30b49a617a585f10863d1210f8c0a3db01e8647ebdaa7dd32"),
            "transitions": (632,
                "307bf3a471ee577b473116e643941f74b8cf1381fe55c97f74db04bc34902550"),
        }
        for key, (count, expected_digest) in aggregate_sequences.items():
            sequence = trace.get(key)
            if not isinstance(sequence, list) or len(sequence) != count or \
                    canonical_sha256(sequence) != expected_digest:
                raise ValueError(f"mainline complete {key} sequence differs")

        save = args.save.read_bytes()
        mapz = args.mapz.read_bytes()
        name = args.name.read_bytes()
        if hashlib.sha256(save).hexdigest() != \
                "538a55286fd20c7d01fa295989f4f6c2c7f49983fc55f5b5c7fbfc2c1364759e" or \
                hashlib.sha256(mapz).hexdigest() != \
                "14a9b1760269f99369b1dcd1bad4c010754f3f1c06275bbb69ff1db563d5a77c" or \
                hashlib.sha256(name).hexdigest() != \
                "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba":
            raise ValueError("persisted mainline artifact SHA-256 differs")
        if len(save) != 1350 or fnv1a64(save) != digests["state_fnv1a64"]:
            raise ValueError("persisted mainline SAVE differs from live state")
        if fnv1a64(mapz) != digests["mapz_fnv1a64"]:
            raise ValueError("persisted mainline MAPZ differs from live database")
        if len(name) != 514 or fnv1a64(name) != digests["name_fnv1a64"]:
            raise ValueError("persisted mainline NAME differs from live font")

        # The Taoist handoff adds flag 36 after all previously proven story
        # flags. CHNA3 event 20 sets flag 56 at the Zhou patron; after entering
        # DAIU1, CHNA2 event 20 sets flag 33 as it changes the water layout.
        # Rain event 42 then sets flag 32, Buddha event 28 sets flag 41, and
        # the completed FIR3 event-80 continuation sets flag 50. HUMA2 event
        # 38 adds flag 39; the first false-immortal meeting adds flag 37 and
        # pot-immortal event 68 adds flag 35 before the return confrontation.
        if u16(save, 0x4A2) != 0xE880:
            raise ValueError("village/Stronghold/river-demon story flags are not exact")
        if u16(save, 0x4A4) != 0x2202 or u16(save, 0x4A6) != 0xFD48:
            raise ValueError(
                "water-control/rain/Buddha story flags are not exact")
        if u16(save, 0x4A8) != 0xA280:
            raise ValueError(
                "stone-lion/patron/mechanism/FIR3 story flags are not exact")
        # CHNA5 event 20 produces flag 90; event 24 produces flag 68;
        # SD01 event 32 produces flag 70 and item 89; AREA3 event 38 consumes
        # that item and produces flag 72.  These are decoded producer/
        # consumer relations, not flags inferred from arriving in ZF.
        if u16(save, 0x4AA) != 0x0A80 or u16(save, 0x4AC) != 0x0020:
            raise ValueError("HOUW3/SD01/item-89 gate story flags are not exact")
        if save[0x521] != 1:
            raise ValueError("far-side AREA1 travel flag three was not set")
        if u16(save, 0x424) != 220:
            raise ValueError("completed ending did not persist directory 220")
        world_x = u16(save, 0x41B) + ((u16(save, 0x012) + 2) >> 1)
        world_y = u16(save, 0x41D) + ((u16(save, 0x02A) + 16) >> 3)
        if (world_x, world_y) != (38, 16):
            raise ValueError(f"completed-ending position is {(world_x, world_y)!r}")
        if u16(save, 0x51C) != 0:
            raise ValueError("RPG did not clear the final post-battle continuation")
        if u16(save, 0x10) != 4 or u16(save, 0x104) != 1163:
            raise ValueError("completed-ending party count or money differs")
        if u16(save, 0x49C) != 0x1914:
            raise ValueError("completed-ending FIG random cursor differs")
        if save[0x529] != 1:
            raise ValueError("AREA2 Jianmu return portal did not set travel flag 11")
        # MAP0 action 4011h sets bit 0010h before event 334; FIR3 action
        # 400ah later adds bit 0001h before event 52. Opcode 3 hides
        # directory-252 entity one, opcode 34 redirects the location-220 mage
        # to event 336, and opcode 58 executes fixed ORC directory 802ch. The
        # legal default-command trace ends in defeat; event 334 contains no
        # victory/defeat branch and OC resumes at that world position. The
        # same process later traverses the released return route and completes
        # the four-treasure mage chain without altering these once-only bits.
        if u16(save, 0x51A) != 0x01FF:
            raise ValueError(
                    "Jianmu/Taotie/FIR3/SD01/T9 MAP0 once-only trigger flags differ")
        expected_party = [
            (0x2000, 0, 156, 122, 122, 94, 94, 328, 843),
            (0x2000, 0, 150, 46, 46, 99, 99, 328, 842),
            (0x2000, 0, 161, 140, 140, 46, 46, 600, 847),
            (0x2000, 0, 153, 50, 50, 94, 94, 263, 838),
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
                    f"completed-ending actor {actor} state is {actual!r}, "
                    f"expected {expected!r}")
        if [u16(save, 0x106 + actor * 0x9F + 0x31)
                for actor in range(4)] != [17, 17, 17, 17]:
            raise ValueError("completed-ending party levels differ")
        expected_inventory = [
            262, 206, 110, 206, 291, 104, 119, 257, 83,
            89, 278, 250, 281, 316,
        ] + [0] * 36
        actual_inventory = [u16(save, 0x382 + slot * 2) for slot in range(50)]
        if actual_inventory != expected_inventory:
            raise ValueError("ending item-259-to-316 transform differs")

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
        if map_field(image, 220, 9, 0) != 456:
            raise ValueError(
                "ending mage did not persist event 340 -> 448 -> 452 -> 454 -> 456")
        # CHNA3 event 82 opens the Zhou-to-DAIU gate by persisting behavior
        # three on entity zero. CHNA2 event 20's opcode-34 list hides the two
        # aliased Zhou residents at byte offsets 14/16. All these location
        # records reference the same released ten-entity MAPZ area.
        for location in (280, 286, 354, 358):
            if map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 3, 7) != 3 or \
                    map_field(image, location, 3, 8) != 3:
                raise ValueError(
                    f"Zhou/DAIU gate mutations differ at location {location}")
        # DAIU1 actor four is an automatic CHNA2 event-478 collision. Event 20
        # then moves the water-control actor, changes its behavior and event,
        # and opcode 37 reloads the aliased area as directory 376.
        for location in (284, 376):
            if map_field(image, location, 2, 0) != 5302 or \
                    map_field(image, location, 3, 0) != 5 or \
                    map_field(image, location, 9, 0) != 44 or \
                    map_field(image, location, 3, 4) != 3:
                raise ValueError(
                    f"DAIU1 water-control mutations differ at location {location}")
        if map_field(image, 364, 9, 0) != 26:
            raise ValueError(
                "compass purchase did not persist patron event 522 -> 26")
        # The first HUMAT audience executes after the rain relocation and
        # changes entity six from CHNA3 directory 152 to 154. At the Buddha
        # entrance event 38 opens entity zero by changing behavior four to
        # three. Event 28 finally redirects the aliased BUIN2 entity zero to
        # event 32 before relocating to the location-590 view of that area.
        if map_field(image, 342, 9, 6) != 154:
            raise ValueError("HUMAT audience did not persist event 152 -> 154")
        if map_field(image, 264, 3, 0) != 3 or \
                map_field(image, 264, 9, 0) != 36:
            raise ValueError("Buddha entrance monk was not persistently opened")
        for location in (320, 324, 332, 590):
            if map_field(image, location, 3, 0) != 7 or \
                    map_field(image, location, 9, 0) != 32:
                raise ValueError(
                    f"Buddha revival redirect differs at location {location}")
        # Raw directory 0052h opens the shared DAUF gate. Event 46 then
        # redirects entity one to event 48 before relocating to directory 676.
        for location in (426, 430, 432, 676):
            if map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 9, 0) != 50 or \
                    map_field(image, location, 3, 1) != 7 or \
                    map_field(image, location, 9, 1) != 48:
                raise ValueError(
                    f"DAUF gate/mechanism mutations differ at location {location}")
        if map_field(image, 252, 3, 1) != 3 or \
                map_field(image, 252, 9, 1) != 334:
            raise ValueError("event 334 did not persistently hide its trigger entity")
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
        # FIR3 action 400ah/event 52 hides entity zero. Events 54 and 78
        # redirect entity one to 78 then 80 before hiding it, and event 80
        # finally hides entity two. These words are read from the persistent
        # released MAPZ area rather than inferred from the final scene.
        if map_field(image, 466, 3, 0) != 3 or \
                map_field(image, 466, 9, 0) != 52 or \
                map_field(image, 466, 3, 1) != 3 or \
                map_field(image, 466, 9, 1) != 80 or \
                map_field(image, 466, 3, 2) != 3:
            raise ValueError("FIR3 event 52/54/78/80 entity chain differs")
        # Flag 50 is consumed by CHNA3 30 -> 34 -> 38 at HUMA2. The latter
        # redirects its actor to event 44 and sets flag 39, which is then read
        # by the CHNA2 72 -> 74 gate in the released T6 room.
        if map_field(image, 340, 3, 0) != 4 or \
                map_field(image, 340, 9, 0) != 44:
            raise ValueError("HUMA2 flag-39 producer did not persist event 44")
        # ZD event 56 redirects and hides the body thief before fixed 801ah.
        # T7 event 34 opens both FUZHO ends; HU04 62/64 hides the body thief,
        # leaves the pot immortal on 66, and restores item 83. CHNA4 event 24
        # leaves the PUDO mouth on event 28, while event 26 remains installed
        # on the real-immortal actor after relocating the party to T6.
        if map_field(image, 498, 3, 0) != 3 or \
                map_field(image, 498, 9, 0) != 70:
            raise ValueError("ZD body-thief redirect differs")
        if any(map_field(image, 516, 3, entity) != 3
               for entity in (0, 1)):
            raise ValueError("T7 FUZHO passages were not both opened")
        if map_field(image, 520, 9, 4) != 412:
            raise ValueError("T7 item-83 actor did not persist event 412")
        if map_field(image, 548, 3, 0) != 3 or \
                map_field(image, 548, 9, 0) != 64:
            raise ValueError("HU04 body-thief event did not persist its hidden state")
        if map_field(image, 584, 3, 0) != 0 or \
                map_field(image, 584, 9, 0) != 66:
            raise ValueError("Z20 pot-immortal event did not persist event 66")
        if map_field(image, 596, 3, 0) != 7 or \
                map_field(image, 596, 9, 0) != 28:
            raise ValueError("PUDO return mouth did not persist event 28")
        if map_field(image, 604, 3, 0) != 5 or \
                map_field(image, 604, 9, 0) != 26:
            raise ValueError("real-immortal actor did not persist event 26")
        # The CHNA5 item-89 prerequisite mutates the released MAPZ records at
        # every step.  Event 20 hides the HOUW3 actor; event 22 opens the two
        # aliased ZG actors; event 24 rewrites the shared FUMOD pair.  Event
        # 38 finally hides the AREA3 gate actor before loading directory 720.
        if map_field(image, 664, 3, 0) != 3:
            raise ValueError("HOUW3 wounded actor was not persistently hidden")
        for location in (668, 672, 674):
            if any(map_field(image, location, 3, entity) != 3
                   for entity in (0, 1)):
                raise ValueError(
                    f"ZG event-22 actor mutations differ at location {location}")
        for location in (724, 730):
            if map_field(image, location, 2, 0) != 57770 or \
                    map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 9, 0) != 182 or \
                    map_field(image, location, 2, 1) != 57796 or \
                    map_field(image, location, 3, 1) != 0 or \
                    map_field(image, location, 9, 1) != 186:
                raise ValueError(
                    f"FUMOD event-24 pair differs at location {location}")
        if map_field(image, 700, 3, 2) != 0:
            raise ValueError("AREA3 item-89 gate actor was not hidden")
        # EAST16 locations 800/824 alias the same released nine-entity area.
        # Event 54 changes the guardian's behavior from four to three before
        # fixed 8034h; event 56 opens entity one, redirects it to event 50 and
        # inserts item 278 into the first free inventory word.
        for location in (800, 824):
            if map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 9, 0) != 54 or \
                    map_field(image, location, 0, 1) != 49 or \
                    map_field(image, location, 9, 1) != 50:
                raise ValueError(
                    f"east-T9 guardian/chest mutations differ at location {location}")
        # WEST12 locations 828/834/848 alias a second released area. Special
        # action 18/event 46 opens guardian entity zero before fixed 802eh;
        # event 48 changes entity one's sprite to 31h, redirects it to event
        # 50 and inserts item 250. These are persisted MAPZ words, not a
        # post-hoc inventory fixture.
        for location in (828, 834, 848):
            if map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 9, 0) != 46 or \
                    map_field(image, location, 0, 1) != 49 or \
                    map_field(image, location, 9, 1) != 50:
                raise ValueError(
                    f"west-T9 guardian/chest mutations differ at location {location}")
        # SOUT56 directories 872/876 likewise alias one nine-entity area.
        # Special action 20/event 58 hides the guardian before fixed 8038h;
        # event 60 opens entity one, redirects it to 50 and produces item 281.
        for location in (872, 876):
            if map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 9, 0) != 58 or \
                    map_field(image, location, 0, 1) != 49 or \
                    map_field(image, location, 9, 1) != 50:
                raise ValueError(
                    f"south-T9 guardian/chest mutations differ at location {location}")
        # NORT89 has five aliases around two paired connector corridors.
        # Action 21/event 322 hides guardian zero before fixed 8032h; event
        # 324 opens chest one, redirects it to 50, and produces item 259.
        for location in (908, 912, 914, 916, 918):
            if map_field(image, location, 3, 0) != 3 or \
                    map_field(image, location, 9, 0) != 322 or \
                    map_field(image, location, 0, 1) != 49 or \
                    map_field(image, location, 9, 1) != 50:
                raise ValueError(
                    f"north-T9 guardian/chest mutations differ at location {location}")
        # CHNA5 event 592 tests all four script-produced treasures, then hides
        # the same gate entity in both NORT77 directory aliases.
        for location in (904, 910):
            if map_field(image, location, 3, 2) != 3 or \
                    map_field(image, location, 9, 2) != 592:
                raise ValueError(
                    f"north-T9 four-treasure gate differs at location {location}")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("mainline ending did not stop through opcode 52")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline prefix validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RPG mainline prefix validation: OK "
        "(new game -> event 334 -> event 336 -> AREA2 -> DAIU1 water control "
        "-> compass -> rain ritual -> Buddha revival -> DAUF mechanism -> "
        "FIR3 events 52/54/78/80 -> flags 39/37/35 -> pot world -> "
        "false immortal -> HOUW3/ZG -> SD01 item 89 -> ZF -> "
        "T9 east item 278 -> T9 west item 250 -> T9 south item 281 -> "
        "T9 north item 259 -> four-treasure gate -> released return route -> "
        "travel items 278/281 -> ending events 448/452/454/456 -> opcode 52)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
