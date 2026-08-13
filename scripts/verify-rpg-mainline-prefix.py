#!/usr/bin/env python3
"""Lock the released main story through the first Great Yu waterway section."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


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
        # return/mine encounters, fixed Taotie encounter 16, then eight legal
        # post-Taotie encounters through the first Great Yu waterway section.
        for index in range(53):
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
            ("input", "total"): 8860,
            ("input", "consumed"): 8860,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 795,
            ("boundaries", "poll"): 8102,
            ("boundaries", "text"): 614,
            ("video", "frames"): 25482,
            ("video", "direct_updates"): 4115,
            ("video", "fnv1a64"): "5f78cab624fe1963",
            ("audio", "music_calls"): 245,
            ("audio", "voice_calls"): 474,
            ("audio", "stop_audio_calls"): 110,
            ("audio", "fnv1a64"): "99b9f4b4fe2d68a5",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 1125133:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "a8b6a177e67267a7",
            "mapz_fnv1a64": "94818e710189bb2f",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 25482 or frames[-1] != "db5a72128f1a448e":
            raise ValueError("mainline final first-inner-waterway frame differs")

        checkpoints = trace.get("input_checkpoints", [])
        expected_checkpoints = {
            123: ("POLL", "CONFIRM", "efe04c584b1a88b8", "827f0f1b725a0958"),
            293: ("POLL", "UP", "430dd5b172859efe", "c0ddff17acca830b"),
            345: ("POLL", "DOWN", "86f36d75184bf502", "2ff9efae32900757"),
            515: ("WAIT", "CONFIRM", "6b2035b69889b10f", "004e684bc1b4f164"),
            519: ("POLL", "UP", "ddaf90d252d244fe", "004e684bc1b4f164"),
            610: ("WAIT", "CONFIRM", "51b99d07ba290357", "004e684bc1b4f164"),
            613: ("WAIT", "CONFIRM", "9eb0493a3aff37b2", "004e684bc1b4f164"),
            740: ("WAIT", "CONFIRM", "60f34fa985dcec85", "004e684bc1b4f164"),
            743: ("WAIT", "CONFIRM", "0357174787b81959", "004e684bc1b4f164"),
            821: ("POLL", "CONFIRM", "92ca94492a32a65f", "004e684bc1b4f164"),
            827: ("WAIT", "LEFT", "49c1a0ed9d826c07", "0f51c5416ef6a73d"),
            901: ("WAIT", "CONFIRM", "5f9bf4514bc149bd", "0f51c5416ef6a73d"),
            902: ("POLL", "DOWN", "e776787f6ae6f5a5", "0f51c5416ef6a73d"),
            1346: ("POLL", "CONFIRM", "9ceec50582d9d537", "0f51c5416ef6a73d"),
            1352: ("POLL", "DOWN", "facdcec8072cdd17", "88435907c045bd63"),
            1496: ("POLL", "NONE", "75edd863c474dafc", "88435907c045bd63"),
            1569: ("POLL", "NONE", "6950c6eda9b97cad", "88435907c045bd63"),
            1796: ("POLL", "CONFIRM", "ed84620a2f236f46", "88435907c045bd63"),
            1799: ("POLL", "UP", "ed84620a2f236f46", "ae6c1bca924a54ac"),
            1851: ("POLL", "NONE", "05e3ae1a8a1cf49e", "ae6c1bca924a54ac"),
            1855: ("WAIT", "DOWN", "0bdb7bc7c2296b61", "ae6c1bca924a54ac"),
            1858: ("WAIT", "CONFIRM", "0bdb7bc7c2296b61", "ae6c1bca924a54ac"),
            1859: ("POLL", "DOWN", "e310b387046e727f", "ae6c1bca924a54ac"),
            1922: ("POLL", "NONE", "b7ff851ac0057323", "ae6c1bca924a54ac"),
            1923: ("POLL", "UP", "b7ff851ac0057323", "ae6c1bca924a54ac"),
            1969: ("POLL", "NONE", "d2be14e5f720dc66", "ae6c1bca924a54ac"),
            2017: ("POLL", "NONE", "9a0ece6fcda24daa", "ae6c1bca924a54ac"),
            2070: ("POLL", "NONE", "81bb69a3e97cad5e", "ae6c1bca924a54ac"),
            2119: ("POLL", "CONFIRM", "0ecf7718b538523d", "ae6c1bca924a54ac"),
            2120: ("WAIT", "CONFIRM", "0ecf7718b538523d", "ae6c1bca924a54ac"),
            2121: ("POLL", "UP", "d6274b10f78f891c", "ae6c1bca924a54ac"),
            2223: ("POLL", "DOWN", "6f99742f0c73ff63", "ae6c1bca924a54ac"),
            2269: ("POLL", "NONE", "735baa81a53beb07", "ae6c1bca924a54ac"),
            2471: ("POLL", "DOWN", "976f4c41ea912414", "ae6c1bca924a54ac"),
            2503: ("WAIT", "DOWN", "d3a6b7d6804f957c", "ae6c1bca924a54ac"),
            2511: ("POLL", "CANCEL", "93fae7418cf33770", "ae6c1bca924a54ac"),
            2527: ("POLL", "RIGHT", "fa7c12a40015c214", "ae6c1bca924a54ac"),
            2607: ("WAIT", "DOWN", "1d7e3854a1113b11", "ae6c1bca924a54ac"),
            2611: ("POLL", "DOWN", "80b77a90f0702b61", "ae6c1bca924a54ac"),
            2703: ("WAIT", "DOWN", "174bafb9d7cf31ff", "ae6c1bca924a54ac"),
            2711: ("POLL", "CANCEL", "acf8feee8c44df81", "ae6c1bca924a54ac"),
            2730: ("POLL", "RIGHT", "76249f8b988cdb49", "ae6c1bca924a54ac"),
            2766: ("POLL", "NONE", "885e3e074de72341", "ae6c1bca924a54ac"),
            2767: ("POLL", "UP", "885e3e074de72341", "ae6c1bca924a54ac"),
            2790: ("POLL", "DOWN", "a81e2e62f4515ba1", "3820efe1b0be688b"),
            2793: ("POLL", "NONE", "238fb42fcddb25ff", "3820efe1b0be688b"),
            2843: ("WAIT", "CONFIRM", "5d110e5fe05dd980", "3820efe1b0be688b"),
            2860: ("WAIT", "CONFIRM", "ea4801003d3d4408", "3820efe1b0be688b"),
            2862: ("WAIT", "CONFIRM", "fc38e6d6d4797db7", "3820efe1b0be688b"),
            2952: ("POLL", "RIGHT", "c4ed7a3a21a5a252", "3820efe1b0be688b"),
            2959: ("POLL", "CANCEL", "295c4da780096e9e", "3820efe1b0be688b"),
            3632: ("POLL", "CANCEL", "b45f62e92a8ea984", "3820efe1b0be688b"),
            3651: ("POLL", "RIGHT", "b2667cbef4706530", "3820efe1b0be688b"),
            4333: ("POLL", "CANCEL", "118375d618880306", "3820efe1b0be688b"),
            4371: ("POLL", "RIGHT", "366eab1301439938", "3820efe1b0be688b"),
            4952: ("POLL", "CANCEL", "44967b9905c24c1b", "3820efe1b0be688b"),
            4990: ("POLL", "RIGHT", "39d32f60ddb03ebf", "3820efe1b0be688b"),
            5111: ("POLL", "RIGHT", "2d40b22260e59d65", "3820efe1b0be688b"),
            5660: ("POLL", "RIGHT", "0da7c6c02d618319", "3820efe1b0be688b"),
            6290: ("POLL", "RIGHT", "c62bfe13ab064fa6", "3820efe1b0be688b"),
            6845: ("POLL", "LEFT", "d8f63e3890b8fc67", "3820efe1b0be688b"),
            6854: ("POLL", "UP", "2360560874a31d48", "3820efe1b0be688b"),
            7027: ("WAIT", "CONFIRM", "7da1a7f0978818e2", "eb4b3375572e040b"),
            7028: ("WAIT", "CONFIRM", "955e932abf6ba075", "eb4b3375572e040b"),
            7029: ("POLL", "CONFIRM", "47665f035a1de0c2", "eb4b3375572e040b"),
            7032: ("POLL", "DOWN", "241c141199d6b002", "eb4b3375572e040b"),
            7162: ("WAIT", "DOWN", "03c35ddd60162750", "eb4b3375572e040b"),
            7303: ("WAIT", "CONFIRM", "e62aa6d1f412fa2c", "eb4b3375572e040b"),
            7400: ("POLL", "LEFT", "0b169ac6a1f5d3b3", "eb4b3375572e040b"),
            7442: ("POLL", "RIGHT", "a0a3291fc84768d3", "eb4b3375572e040b"),
            7665: ("WAIT", "DOWN", "1ec2ba6b2933bfb2", "eb4b3375572e040b"),
            7807: ("WAIT", "DOWN", "6c5f56b697b8edaa", "eb4b3375572e040b"),
            7879: ("POLL", "CONFIRM", "477aa39066c5b525", "eb4b3375572e040b"),
            7880: ("WAIT", "LEFT", "c8d2b36b8a5ca675", "65524db874b766dc"),
            7985: ("WAIT", "CONFIRM", "5ffd3935ea073a53", "65524db874b766dc"),
            8026: ("POLL", "CONFIRM", "03b63d2186e98146", "65524db874b766dc"),
            8035: ("POLL", "LEFT", "fea2a4b299109254", "760c33e95c6bbb40"),
            8076: ("WAIT", "DOWN", "31deeac5b40b2e4a", "760c33e95c6bbb40"),
            8187: ("WAIT", "DOWN", "9044363c8126a4e4", "760c33e95c6bbb40"),
            8274: ("WAIT", "DOWN", "24fe3f9e70800f3a", "760c33e95c6bbb40"),
            8307: ("POLL", "RIGHT", "0ade2862064a94a0", "760c33e95c6bbb40"),
            8358: ("WAIT", "DOWN", "68f09cd08af127f2", "760c33e95c6bbb40"),
            8421: ("POLL", "UP", "fbd23e3fbc3b6f94", "760c33e95c6bbb40"),
            8436: ("WAIT", "DOWN", "5cbc27ae3bd76099", "760c33e95c6bbb40"),
            8542: ("POLL", "CONFIRM", "8b8d7494025249db", "760c33e95c6bbb40"),
            8547: ("POLL", "DOWN", "8b8d7494025249db", "94818e710189bb2f"),
            8575: ("WAIT", "DOWN", "027c0ce2ff784cdd", "94818e710189bb2f"),
            8667: ("WAIT", "DOWN", "ee4f677f8ac5675f", "94818e710189bb2f"),
            8673: ("POLL", "LEFT", "9cf8bbda62520fe7", "94818e710189bb2f"),
            8716: ("POLL", "UP", "de9c5a84ba4859ef", "94818e710189bb2f"),
            8783: ("POLL", "UP", "c693ed1273969dce", "94818e710189bb2f"),
            8815: ("WAIT", "DOWN", "e5fd32adeee04729", "94818e710189bb2f"),
            8859: ("POLL", "QUIT", "a8b6a177e67267a7", "94818e710189bb2f"),
        }
        if len(checkpoints) != 8860:
            raise ValueError("mainline input checkpoint count differs")
        for index, expected in expected_checkpoints.items():
            item = checkpoints[index]
            actual = (
                item.get("boundary"), item.get("action"),
                item.get("state_fnv1a64"), item.get("mapz_fnv1a64"),
            )
            if actual != expected:
                raise ValueError(
                    f"mainline checkpoint {index} is {actual!r}, expected {expected!r}")

        expected_positions = {
            902: (14, 112, 29, 3),
            1346: (10, 59, 78, 6),
            1496: (12, 71, 119, 3),
            1569: (14, 122, 165, 3),
            1796: (14, 84, 29, 6),
            1851: (66, 123, 56, 0),
            1855: (66, 124, 57, 0),
            1922: (74, 96, 153, 3),
            1969: (90, 36, 36, 3),
            2017: (104, 118, 131, 0),
            2070: (88, 32, 114, 3),
            2121: (88, 45, 45, 0),
            2223: (108, 29, 75, 0),
            2269: (102, 117, 109, 0),
            2471: (110, 48, 31, 3),
            2503: (110, 62, 49, 9),
            2607: (110, 95, 94, 0),
            2703: (110, 149, 126, 9),
            2766: (120, 61, 37, 3),
            2767: (120, 61, 37, 3),
            2790: (120, 60, 36, 6),
            2793: (122, 165, 108, 0),
            2843: (122, 135, 127, 6),
            2862: (122, 135, 127, 6),
            2952: (122, 75, 98, 6),
            2959: (122, 76, 98, 9),
            3632: (122, 85, 98, 9),
            3651: (122, 85, 98, 9),
            4333: (122, 76, 98, 6),
            4371: (122, 76, 98, 6),
            4952: (122, 87, 98, 6),
            4990: (122, 87, 98, 6),
            5111: (122, 90, 98, 9),
            5660: (122, 91, 98, 9),
            6290: (122, 82, 98, 6),
            6845: (122, 84, 98, 6),
            6854: (122, 75, 98, 6),
            7028: (122, 75, 98, 3),
            7029: (588, 76, 98, 0),
            7032: (588, 76, 98, 0),
            7162: (588, 74, 166, 0),
            7303: (128, 44, 166, 6),
            7400: (136, 65, 99, 3),
            7442: (140, 32, 114, 3),
            7665: (132, 52, 156, 3),
            7807: (132, 70, 88, 9),
            7879: (132, 107, 88, 0),
            7880: (132, 107, 88, 0),
            7985: (132, 107, 88, 0),
            8026: (132, 121, 114, 9),
            8035: (132, 121, 114, 9),
            8076: (132, 107, 87, 3),
            8187: (132, 31, 94, 6),
            8274: (132, 53, 149, 0),
            8307: (134, 85, 86, 0),
            8358: (134, 85, 134, 6),
            8421: (130, 45, 164, 3),
            8436: (130, 45, 149, 3),
            8542: (130, 50, 56, 3),
            8547: (130, 50, 56, 3),
            8575: (130, 50, 84, 0),
            8667: (130, 46, 166, 0),
            8673: (116, 114, 157, 0),
            8716: (176, 53, 177, 3),
            8783: (180, 51, 116, 3),
            8815: (180, 38, 133, 0),
            8859: (184, 120, 132, 0),
        }
        for index, expected in expected_positions.items():
            item = checkpoints[index]
            actual = (item.get("map_location"), item.get("world_x"),
                      item.get("world_y"), item.get("actor_direction"))
            if actual != expected:
                raise ValueError(
                    f"mainline world checkpoint {index} is {actual!r}, "
                    f"expected {expected!r}")

        save = args.save.read_bytes()
        mapz = args.mapz.read_bytes()
        name = args.name.read_bytes()
        if len(save) != 1350 or fnv1a64(save) != digests["state_fnv1a64"]:
            raise ValueError("persisted mainline SAVE differs from live state")
        if fnv1a64(mapz) != digests["mapz_fnv1a64"]:
            raise ValueError("persisted mainline MAPZ differs from live database")
        if len(name) != 514 or fnv1a64(name) != digests["name_fnv1a64"]:
            raise ValueError("persisted mainline NAME differs from live font")

        # Event 338 and the Great Yu tablet add their flags after the
        # already-proven village, Stronghold, mayor, boat and stone-lion bits.
        if u16(save, 0x4A2) != 0xE880:
            raise ValueError("village/Stronghold/river-demon story flags are not exact")
        if u16(save, 0x4A4) != 0x2202 or u16(save, 0x4A6) != 0x0008:
            raise ValueError("freed-father/mayor/boat/tablet story flags are not exact")
        if u16(save, 0x4A8) != 0x8000:
            raise ValueError("ONE3A stone-lion story flag 48 was not set")
        if save[0x521] != 1:
            raise ValueError("far-side AREA1 travel flag three was not set")
        if u16(save, 0x424) != 184:
            raise ValueError("first inner waterway checkpoint is not directory 184")
        world_x = u16(save, 0x41B) + ((u16(save, 0x012) + 2) >> 1)
        world_y = u16(save, 0x41D) + ((u16(save, 0x02A) + 16) >> 3)
        if (world_x, world_y) != (120, 132):
            raise ValueError(f"first inner waterway position is {(world_x, world_y)!r}")
        if u16(save, 0x51C) != 0:
            raise ValueError("RPG did not consume and clear the post-battle entity continuation")
        if u16(save, 0x10) != 3 or u16(save, 0x104) != 3011:
            raise ValueError("post-Taotie party count or money differs")
        if u16(save, 0x49C) != 0x1000:
            raise ValueError("first-inner-waterway FIG random cursor differs")
        if u16(save, 0x51A) != 0x0004:
            raise ValueError("Taotie MAP0 once-only trigger flag was not retained")
        expected_party = [
            (0x1000, 16, 123, 101, 101, 45, 75, 161, 511),
            (0x0000, 124, 125, 35, 35, 4, 83, 148, 509),
            (0x0000, 16, 134, 11, 118, 35, 35, 430, 514),
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
                    f"post-Taotie actor {actor} state is {actual!r}, "
                    f"expected {expected!r}")
        if [u16(save, 0x106 + actor * 0x9F + 0x31)
                for actor in range(3)] != [15, 15, 15]:
            raise ValueError("legal ONE2A training did not reach level 15")

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
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("mainline checkpoint did not stop at explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline prefix validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RPG mainline prefix validation: OK "
        "(village -> Fire-Eyed Suanni -> T2 -> stone lions -> river demon -> Taotie -> Great Yu tablet -> waterway 184)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
