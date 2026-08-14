#!/usr/bin/env python3
"""Lock the continuous legal route through T8 and the four-treasure T9 gate."""

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
        # The released route reaches every boundary without a checkpoint
        # reload. The FIR3, pot-world, false-immortal and item-89 chains
        # include natural encounters plus fixed 8016h/8018h/8030h/801ah/
        # 801ch/801eh/8022h/8024h/4040h battles, then the ZF/T9 route and
        # EAST16 event 54/8034h, WEST12 event 46/802eh, SOUT56 event
        # 58/8038h and NORT89 event 322/8032h; every one is driven by released
        # commands and RNG. FIG's
        # released defeat epilogue returns OC, so the late trace deliberately
        # records those legal loss paths rather than editing HP.
        for index in range(283):
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
            ("input", "total"): 53580,
            ("input", "consumed"): 53580,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 2342,
            ("boundaries", "poll"): 51270,
            ("boundaries", "text"): 6348,
            ("video", "frames"): 106081,
            ("video", "direct_updates"): 19465,
            ("video", "fnv1a64"): "7e20fb9bd554317c",
            ("audio", "music_calls"): 1234,
            ("audio", "voice_calls"): 958,
            ("audio", "stop_music_calls"): 26,
            ("audio", "stop_audio_calls"): 570,
            ("audio", "fnv1a64"): "a48580db6e149ed7",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 4145983:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "01736596ab541f2d",
            "mapz_fnv1a64": "91be609a29c61b1d",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 106081 or frames[-1] != "5ee91f6a07f92573":
            raise ValueError("mainline post-four-treasure frame differs")
        expected_final_flags = [
            0, 1, 2, 4, 8, 18, 22, 30, 32, 33, 34, 35, 36, 37,
            39, 41, 44, 48, 50, 54, 56, 68, 70, 72, 90,
        ]
        expected_final_inventory = [
            262, 206, 110, 206, 291, 104, 119, 257, 83, 89, 278, 250, 281,
            259,
        ] + [0] * 36
        final_state = trace.get("final_state", {})
        if (final_state.get("map_location"), final_state.get("world_x"),
                final_state.get("world_y"), final_state.get("actor_direction"),
                final_state.get("battle_auxiliary")) != (920, 36, 51, 0, 0):
            raise ValueError("mainline final-state location/battle boundary differs")
        if final_state.get("story_flags") != expected_final_flags:
            raise ValueError("mainline final-state story flags differ")
        if final_state.get("inventory") != expected_final_inventory:
            raise ValueError("mainline final-state four-treasure inventory differs")

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
            8325: ("POLL", "LEFT", "79ae822da3ee5f4d", "760c33e95c6bbb40"),
            8367: ("POLL", "RIGHT", "1223268f047a3ad9", "760c33e95c6bbb40"),
            8415: ("WAIT", "CONFIRM", "03bba5a4988a4547", "760c33e95c6bbb40"),
            8501: ("POLL", "LEFT", "63a4346e05c51c2a", "760c33e95c6bbb40"),
            8513: ("POLL", "DOWN", "41435dd6d1a3a752", "760c33e95c6bbb40"),
            8556: ("POLL", "DOWN", "cb4ff85ccc0f07ed", "760c33e95c6bbb40"),
            8591: ("WAIT", "DOWN", "ff353f6ec799b4f9", "760c33e95c6bbb40"),
            8655: ("POLL", "UP", "b5bc7b51af0f82a8", "760c33e95c6bbb40"),
            8671: ("WAIT", "DOWN", "347288f9979bf108", "760c33e95c6bbb40"),
            8777: ("POLL", "CONFIRM", "4bca1ba0d6d531a3", "760c33e95c6bbb40"),
            8778: ("POLL", "CONFIRM", "4bca1ba0d6d531a3", "ac4bfe2d15e586cc"),
            8782: ("POLL", "DOWN", "4bca1ba0d6d531a3", "94818e710189bb2f"),
            8810: ("WAIT", "DOWN", "55b81bb5030a3cea", "94818e710189bb2f"),
            8902: ("WAIT", "DOWN", "17a7b03cc2aa9295", "94818e710189bb2f"),
            8911: ("POLL", "DOWN", "cd11aeb82d489a9b", "94818e710189bb2f"),
            8956: ("POLL", "UP", "91176114dcdcc56c", "94818e710189bb2f"),
            9023: ("POLL", "UP", "0a0bcfbc2ac32aab", "94818e710189bb2f"),
            9054: ("WAIT", "DOWN", "1838b690c3f58190", "94818e710189bb2f"),
            9099: ("POLL", "DOWN", "939e5add24241a7a", "94818e710189bb2f"),
            9132: ("WAIT", "DOWN", "5c2af7e4200d35ec", "94818e710189bb2f"),
            9136: ("POLL", "LEFT", "bef7e185bb7ec229", "94818e710189bb2f"),
            9240: ("WAIT", "DOWN", "90c9d60e22bd3ae5", "94818e710189bb2f"),
            9244: ("POLL", "RIGHT", "38e1cad395e4266f", "94818e710189bb2f"),
            9313: ("POLL", "CANCEL", "ca96497701e85543", "94818e710189bb2f"),
            9321: ("WAIT", "CONFIRM", "0ebdc3ef780915e2", "94818e710189bb2f"),
            9332: ("POLL", "CANCEL", "809ca2980ac05d86", "94818e710189bb2f"),
            9340: ("WAIT", "CONFIRM", "7819f6a3336fa60f", "94818e710189bb2f"),
            9351: ("POLL", "UP", "d0a68add82fa62bd", "94818e710189bb2f"),
            9377: ("WAIT", "LEFT", "2a1834c7ddffd209", "94818e710189bb2f"),
            9411: ("WAIT", "CONFIRM", "39d0e669f365cc40", "94818e710189bb2f"),
            9412: ("WAIT", "CONFIRM", "2b193a7cade8bc70", "94818e710189bb2f"),
            9413: ("POLL", "UP", "2b193a7cade8bc70", "94818e710189bb2f"),
            9469: ("POLL", "CANCEL", "4208e968905a0b8c", "94818e710189bb2f"),
            9505: ("WAIT", "LEFT", "d82bae0b32b5ee8a", "94818e710189bb2f"),
            9533: ("WAIT", "CONFIRM", "4961fe99bdb52c11", "94818e710189bb2f"),
            9657: ("WAIT", "CONFIRM", "c6ee7bf5604f8989", "94818e710189bb2f"),
            9698: ("WAIT", "CONFIRM", "78eb894fe06b3352", "94818e710189bb2f"),
            9800: ("WAIT", "LEFT", "b44fc211317ac530", "94818e710189bb2f"),
            9828: ("WAIT", "CONFIRM", "2375ee363c1c733f", "94818e710189bb2f"),
            9832: ("POLL", "UP", "f16b738b806b1ff8", "94818e710189bb2f"),
            9866: ("POLL", "CONFIRM", "75803a4a2e5ac9a3", "94818e710189bb2f"),
            9891: ("WAIT", "LEFT", "636783f32b78aa47", "f5452f802bfc8fbf"),
            9922: ("WAIT", "CONFIRM", "fda61b81baa5c102", "f5452f802bfc8fbf"),
            9959: ("POLL", "DOWN", "29c54fe7c3e9c097", "f5452f802bfc8fbf"),
            10030: ("WAIT", "CONFIRM", "3ffde1316da6d9a8", "f5452f802bfc8fbf"),
            10031: ("POLL", "RIGHT", "3ffde1316da6d9a8", "f5452f802bfc8fbf"),
            10138: ("WAIT", "CONFIRM", "b3b2892dbe1835c8", "f5452f802bfc8fbf"),
            10139: ("WAIT", "CONFIRM", "01c40735620062a5", "f5452f802bfc8fbf"),
            10140: ("POLL", "LEFT", "01c40735620062a5", "f5452f802bfc8fbf"),
            10262: ("WAIT", "CONFIRM", "8f36684a35cddd55", "f5452f802bfc8fbf"),
            10273: ("POLL", "CANCEL", "61ede0528d40565f", "f5452f802bfc8fbf"),
            10311: ("POLL", "DOWN", "61e195c239558c3b", "f5452f802bfc8fbf"),
            10420: ("WAIT", "CONFIRM", "f292bebcfd84e877", "f5452f802bfc8fbf"),
            10421: ("POLL", "LEFT", "f292bebcfd84e877", "f5452f802bfc8fbf"),
            10539: ("WAIT", "CONFIRM", "fd9382ba67927f30", "f5452f802bfc8fbf"),
            10599: ("POLL", "UP", "9479a103ef3b8781", "f5452f802bfc8fbf"),
            10664: ("WAIT", "CONFIRM", "1b334c16f96ad022", "f5452f802bfc8fbf"),
            10701: ("POLL", "DOWN", "6e8aae1f1797b5db", "f5452f802bfc8fbf"),
            10769: ("POLL", "DOWN", "7f7b26a9fb1907c6", "f5452f802bfc8fbf"),
            10770: ("POLL", "LEFT", "353c5e35890cee26", "f5452f802bfc8fbf"),
            10840: ("WAIT", "CONFIRM", "64bb00951eb15db5", "f5452f802bfc8fbf"),
            10858: ("POLL", "LEFT", "b1fc61f758852b23", "f5452f802bfc8fbf"),
            11001: ("WAIT", "CONFIRM", "477de15c509ec382", "f5452f802bfc8fbf"),
            11036: ("POLL", "UP", "0730cd1abf4a35a6", "f5452f802bfc8fbf"),
            11060: ("POLL", "UP", "fc829433de669bea", "f5452f802bfc8fbf"),
            11083: ("POLL", "CONFIRM", "16040ae5e5d2dbe0", "f5452f802bfc8fbf"),
            11086: ("POLL", "DOWN", "f930cb1d1a997e48", "f5452f802bfc8fbf"),
            11108: ("POLL", "RIGHT", "6c57fc8752351472", "f5452f802bfc8fbf"),
            11125: ("POLL", "NONE", "53543ed5fe942304", "ebac54aa8c810d33"),
            11126: ("POLL", "UP", "59edb911a01ef098", "ebac54aa8c810d33"),
            11168: ("POLL", "UP", "c02b6d6da1ea1277", "ebac54aa8c810d33"),
            11195: ("POLL", "CONFIRM", "f09bf423ca95e331", "ebac54aa8c810d33"),
            11197: ("WAIT", "LEFT", "4f8f46f9423b47d0", "eb4d06993c03bdca"),
            11311: ("WAIT", "CONFIRM", "3cf5e2e6af23b4da", "eb4d06993c03bdca"),
            11312: ("POLL", "CONFIRM", "72afb064fd46af96", "eb4d06993c03bdca"),
            11324: ("POLL", "CONFIRM", "72afb064fd46af96", "8853834e394c715a"),
            11325: ("POLL", "DOWN", "72afb064fd46af96", "8853834e394c715a"),
            11354: ("POLL", "DOWN", "ead8f2b7e4134f13", "8853834e394c715a"),
            11402: ("POLL", "DOWN", "620bca46f19916ff", "8853834e394c715a"),
            11459: ("WAIT", "LEFT", "69616cd597189d66", "8853834e394c715a"),
            11568: ("POLL", "RIGHT", "4352cafc03e6309b", "8853834e394c715a"),
            11585: ("POLL", "DOWN", "7a19e27cc94eebe8", "8853834e394c715a"),
            11588: ("WAIT", "LEFT", "daf72f05bc3139eb", "8853834e394c715a"),
            11608: ("POLL", "DOWN", "d0d944fee2fc44ed", "8853834e394c715a"),
            11694: ("WAIT", "DOWN", "82a925ae093e925e", "8853834e394c715a"),
            11698: ("POLL", "LEFT", "0f5fa090b5e9a53a", "8853834e394c715a"),
            11800: ("WAIT", "DOWN", "04fbf50215fda750", "8853834e394c715a"),
            11804: ("POLL", "RIGHT", "c6cc3b07d24d233f", "8853834e394c715a"),
            11867: ("POLL", "DOWN", "c789ec1acbeb94d3", "8853834e394c715a"),
            11869: ("POLL", "DOWN", "c2bb0141cf94120f", "8853834e394c715a"),
            11923: ("WAIT", "DOWN", "5abb0f7f61edf04b", "8853834e394c715a"),
            11927: ("POLL", "LEFT", "4aa6e420e0b468f1", "8853834e394c715a"),
            11930: ("POLL", "UP", "adc0fdd63e679bd4", "8853834e394c715a"),
            11962: ("POLL", "CONFIRM", "66c75cf880887230", "8853834e394c715a"),
            12023: ("POLL", "CONFIRM", "7aa0d3b4976d7166", "8a4099cf2d44f30b"),
            12029: ("POLL", "CONFIRM", "940576c2daf6b024", "8db92a0ebc507ddc"),
            12095: ("POLL", "CONFIRM", "c6dbfab0a9d44744", "5eb6808d9dccc796"),
            12165: ("POLL", "CONFIRM", "85f8e0c7ee36147e", "8df34b399eefa5e2"),
            12221: ("POLL", "CONFIRM", "279d6d546e87f6cd", "482c73e5845e52a9"),
            12250: ("POLL", "CONFIRM", "751a70fd6dc877f7", "62d6b9a16969d7ec"),
            12363: ("POLL", "CONFIRM", "054173eb440bbe34", "0ff829508f01112b"),
            12366: ("POLL", "CONFIRM", "6b45461a97db6764", "0ff829508f01112b"),
            12391: ("POLL", "CONFIRM", "56d8d6c1cbed9795", "cb9d0a56c2442bf3"),
            12485: ("POLL", "CONFIRM", "e8df9c408029879d", "71248464a94ce452"),
            12487: ("TEXT", "CONFIRM", "e8df9c408029879d", "7424c8843c5b88b9"),
            12489: ("WAIT", "CONFIRM", "ea3d6b70cb99b959", "4692decfa52e471d"),
            12512: ("WAIT", "CONFIRM", "ea3d6b70cb99b959", "4692decfa52e471d"),
            12513: ("POLL", "LEFT", "79cbe260b2fb6ce4", "4692decfa52e471d"),
            12528: ("POLL", "RIGHT", "20fbd1da45e45e7a", "4692decfa52e471d"),
            13272: ("POLL", "RIGHT", "f60254a881c7423f", "4692decfa52e471d"),
            13300: ("POLL", "CONFIRM", "eec8fba9254358b5", "4692decfa52e471d"),
            13301: ("POLL", "CONFIRM", "eec8fba9254358b5", "1a617f3a6c3be899"),
            13305: ("POLL", "LEFT", "eec8fba9254358b5", "1a617f3a6c3be899"),
            14250: ("POLL", "UP", "ef53d45d46cc7285", "1a617f3a6c3be899"),
            14251: ("POLL", "UP", "120ddf114a7eca45", "1a617f3a6c3be899"),
            14252: ("POLL", "LEFT", "8e9b9c2710e38777", "1a617f3a6c3be899"),
            14286: ("POLL", "LEFT", "94f6d1464da5f093", "1a617f3a6c3be899"),
            14391: ("POLL", "CONFIRM", "9295ff471905a67e", "1a617f3a6c3be899"),
            14407: ("POLL", "CONFIRM", "c72c5bbe9b61e6fe", "1a617f3a6c3be899"),
            14448: ("POLL", "DOWN", "913096258ccd1ff2", "1a617f3a6c3be899"),
            14520: ("POLL", "UP", "3bda680beca0e6d6", "320ce6bf101700fb"),
            14527: ("POLL", "LEFT", "5d37dede566947d9", "320ce6bf101700fb"),
            14925: ("WAIT", "CONFIRM", "992ed821bc2581fa", "320ce6bf101700fb"),
            14926: ("TEXT", "CONFIRM", "c6df828747ca066e", "320ce6bf101700fb"),
            14928: ("POLL", "RIGHT", "f57f015cb7cc9768", "320ce6bf101700fb"),
            14974: ("WAIT", "DOWN", "64f70c7a0809b68e", "320ce6bf101700fb"),
            15074: ("WAIT", "DOWN", "11cb9942ff13878a", "320ce6bf101700fb"),
            15186: ("WAIT", "DOWN", "ff6133644d4cbd7d", "320ce6bf101700fb"),
            15190: ("WAIT", "DOWN", "ff6133644d4cbd7d", "320ce6bf101700fb"),
            15234: ("POLL", "UP", "576da7f02ecf1891", "9e30ceac1ecfe93f"),
            15324: ("POLL", "CONFIRM", "46098f1c0a6e493c", "9e30ceac1ecfe93f"),
            15326: ("POLL", "DOWN", "ad9d13602ed93d55", "93204e39a66528bf"),
            15331: ("WAIT", "DOWN", "924635f649e8d3e2", "93204e39a66528bf"),
            15467: ("WAIT", "DOWN", "895af84a6513c4dc", "93204e39a66528bf"),
            15600: ("WAIT", "DOWN", "69ef028200841ea3", "93204e39a66528bf"),
            15710: ("WAIT", "DOWN", "176e39baa2d96eb1", "93204e39a66528bf"),
            15821: ("WAIT", "DOWN", "3cf680d54dff6ce9", "93204e39a66528bf"),
            15955: ("WAIT", "DOWN", "cfe36e9bdef648fb", "93204e39a66528bf"),
            15959: ("WAIT", "DOWN", "cfe36e9bdef648fb", "93204e39a66528bf"),
            15963: ("WAIT", "DOWN", "cfe36e9bdef648fb", "93204e39a66528bf"),
            15988: ("POLL", "LEFT", "2686352ca54fae40", "93204e39a66528bf"),
            16008: ("POLL", "DOWN", "ee5f4e321e825f9c", "93204e39a66528bf"),
            16065: ("POLL", "RIGHT", "a5dee1a02bb1ba80", "93204e39a66528bf"),
            16087: ("POLL", "CONFIRM", "c083fe607660ad15", "93204e39a66528bf"),
            16100: ("WAIT", "CONFIRM", "c083fe607660ad15", "93204e39a66528bf"),
            16101: ("POLL", "CONFIRM", "688dc2c902c3d8f9", "93204e39a66528bf"),
            16102: ("POLL", "LEFT", "1d392680af3022d6", "1ce5c5e974002091"),
            # Compass -> rain ritual -> first HUMAT audience. The two MAPZ
            # changes distinguish event 42's scene mutation from event 152's
            # persistent entity redirect rather than inferring either from
            # the final directory alone.
            16326: ("POLL", "LEFT", "bba2f44ddb533a2a", "3162ef986cebc8d5"),
            16736: ("POLL", "CONFIRM", "1b43a4a2fc2945c6", "794b60c7b65e93cf"),
            16835: ("POLL", "CONFIRM", "c210fe7731d77169", "ada1882d2c1d1177"),
            16846: ("POLL", "RIGHT", "dedd745ce6106621", "afd1a3598433b9dd"),
            # Continuous-stream FIG boundaries on the road to and inside the
            # Buddha maps. These are not checkpoint-resume approximations.
            16998: ("WAIT", "DOWN", "191e610a921298f1", "afd1a3598433b9dd"),
            17001: ("WAIT", "CONFIRM", "191e610a921298f1", "afd1a3598433b9dd"),
            17107: ("POLL", "CONFIRM", "848cf70c68a8db0b", "afd1a3598433b9dd"),
            17109: ("POLL", "LEFT", "848cf70c68a8db0b", "e5dc4894c05e48b2"),
            17174: ("WAIT", "DOWN", "53d7570ba3bfe57a", "e5dc4894c05e48b2"),
            17189: ("WAIT", "CONFIRM", "53d7570ba3bfe57a", "e5dc4894c05e48b2"),
            17321: ("WAIT", "DOWN", "9249ce168dd3adaf", "e5dc4894c05e48b2"),
            17336: ("WAIT", "CONFIRM", "9249ce168dd3adaf", "e5dc4894c05e48b2"),
            17470: ("WAIT", "DOWN", "f48d977be81101c2", "e5dc4894c05e48b2"),
            17471: ("WAIT", "CONFIRM", "f48d977be81101c2", "e5dc4894c05e48b2"),
            17653: ("WAIT", "DOWN", "c1657d681f06a818", "e5dc4894c05e48b2"),
            17654: ("WAIT", "CONFIRM", "c1657d681f06a818", "e5dc4894c05e48b2"),
            17785: ("WAIT", "DOWN", "12cc31106c0a1a73", "e5dc4894c05e48b2"),
            17786: ("WAIT", "CONFIRM", "12cc31106c0a1a73", "e5dc4894c05e48b2"),
            17939: ("WAIT", "DOWN", "cf6d5bc7f9dd16c5", "e5dc4894c05e48b2"),
            17946: ("WAIT", "CONFIRM", "cf6d5bc7f9dd16c5", "e5dc4894c05e48b2"),
            18077: ("WAIT", "DOWN", "d934a4b1551fe993", "e5dc4894c05e48b2"),
            18080: ("WAIT", "CONFIRM", "d934a4b1551fe993", "e5dc4894c05e48b2"),
            18237: ("WAIT", "DOWN", "c357154acc61d097", "e5dc4894c05e48b2"),
            18240: ("WAIT", "CONFIRM", "c357154acc61d097", "e5dc4894c05e48b2"),
            18241: ("POLL", "NONE", "b8f3379c764cfe7d", "e5dc4894c05e48b2"),
            # CHNA2 event 28 has completed before the route leaves location
            # 590; subsequent checkpoints lock the released return encounters,
            # DAUF gate/event mutations and fire-route entry.
            18316: ("POLL", "RIGHT", "d2790e040a6594fa", "e70220eeeabfc2ae"),
            18403: ("WAIT", "DOWN", "a488f44753105b61", "e70220eeeabfc2ae"),
            18523: ("WAIT", "DOWN", "9a6aafd5f7ac32ce", "e70220eeeabfc2ae"),
            18828: ("WAIT", "DOWN", "002e729167d3b2fd", "e70220eeeabfc2ae"),
            19132: ("WAIT", "DOWN", "71f064830fc4c475", "e70220eeeabfc2ae"),
            19268: ("WAIT", "DOWN", "43780c5f888318dd", "e70220eeeabfc2ae"),
            19418: ("WAIT", "DOWN", "eca2684121f52359", "e70220eeeabfc2ae"),
            19617: ("WAIT", "DOWN", "a204447055b1f27b", "e70220eeeabfc2ae"),
            19764: ("POLL", "UP", "05bc0ce696a2cb4e", "968a48b1f50adca0"),
            19773: ("POLL", "DOWN", "abe2c64db36f8b94", "f9d0dc9efabbed1e"),
            19916: ("WAIT", "DOWN", "a27df2fd2d386cfe", "f9d0dc9efabbed1e"),
            20051: ("WAIT", "DOWN", "e0829cdd480291bb", "f9d0dc9efabbed1e"),
            20105: ("POLL", "CANCEL", "39bb552be347c247", "f9d0dc9efabbed1e"),
            20962: ("WAIT", "DOWN", "aea32b9143ae5c85", "f9d0dc9efabbed1e"),
            21068: ("POLL", "CANCEL", "bd20378aecfc3a66", "f9d0dc9efabbed1e"),
            21092: ("POLL", "UP", "86f3f043a1306b08", "f9d0dc9efabbed1e"),
            21093: ("WAIT", "DOWN", "4998b27b023adadc", "f9d0dc9efabbed1e"),
            21120: ("POLL", "UP", "1bf03a52db9b7944", "f9d0dc9efabbed1e"),
            21200: ("POLL", "RIGHT", "4a6f58b1bc171324", "f9d0dc9efabbed1e"),
            21369: ("WAIT", "DOWN", "b737b458ad2455a9", "f9d0dc9efabbed1e"),
            21535: ("WAIT", "DOWN", "6f1a86f71ae8d1d1", "f9d0dc9efabbed1e"),
            21679: ("WAIT", "DOWN", "77d6d88c853c0809", "f9d0dc9efabbed1e"),
            21698: ("POLL", "UP", "30f06c96d9645e82", "f9d0dc9efabbed1e"),
            21785: ("POLL", "RIGHT", "a8957d3a51062cfa", "f9d0dc9efabbed1e"),
            21786: ("WAIT", "DOWN", "dadad400474a621d", "f9d0dc9efabbed1e"),
            21929: ("POLL", "RIGHT", "ae7e7d2b4215d877", "f9d0dc9efabbed1e"),
            21930: ("WAIT", "DOWN", "c02f54dcd1770ba3", "f9d0dc9efabbed1e"),
            22065: ("POLL", "UP", "cd18996f8bda0349", "f9d0dc9efabbed1e"),
            22066: ("POLL", "UP", "929898d2bd7681ba", "f9d0dc9efabbed1e"),
            22067: ("POLL", "CONFIRM", "e9b06aa726840043", "f9d0dc9efabbed1e"),
            22092: ("POLL", "CONFIRM", "e9b06aa726840043", "18cf034503a645b0"),
            22093: ("WAIT", "DOWN", "364567d4a4bf6251", "18cf034503a645b0"),
            22104: ("WAIT", "CONFIRM", "364567d4a4bf6251", "18cf034503a645b0"),
            22105: ("POLL", "CONFIRM", "92b25693ca70d5ab", "18cf034503a645b0"),
            22106: ("POLL", "CONFIRM", "92b25693ca70d5ab", "18cf034503a645b0"),
            22107: ("WAIT", "DOWN", "0d685d8bd5f0f9c7", "0e7e1c8f4335fb48"),
            22138: ("WAIT", "CONFIRM", "0d685d8bd5f0f9c7", "0e7e1c8f4335fb48"),
            22139: ("POLL", "CONFIRM", "035ddeb50f3fb08e", "0e7e1c8f4335fb48"),
            22140: ("POLL", "CONFIRM", "035ddeb50f3fb08e", "0e7e1c8f4335fb48"),
            22141: ("WAIT", "DOWN", "579a397aa04b79de", "ae97c45e9a7f3e11"),
            22168: ("WAIT", "CONFIRM", "579a397aa04b79de", "ae97c45e9a7f3e11"),
            22169: ("POLL", "CONFIRM", "8cc62445f7d88e89", "ae97c45e9a7f3e11"),
            22178: ("POLL", "CONFIRM", "182b0357bd6ca461", "1d0eec73937d37da"),
            22186: ("POLL", "CONFIRM", "182b0357bd6ca461", "1d0eec73937d37da"),
            22187: ("POLL", "DOWN", "5e0b40bb8c080c41", "1d0eec73937d37da"),
            24387: ("POLL", "DOWN", "9dbf3f211709f214", "4982984b41afc248"),
            25049: ("POLL", "NONE", "2ceb2ebc078627a5", "4982984b41afc248"),
            25416: ("POLL", "CONFIRM", "f202a7377ac4377d", "8684996ddc4139f5"),
            25721: ("POLL", "CONFIRM", "e7cee8906e7dd930", "a7a30e58554d7d6d"),
            26729: ("POLL", "NONE", "56c0d318592ff586", "255d058598cfb5aa"),
            27267: ("POLL", "NONE", "c732f2015a59e703", "255d058598cfb5aa"),
            27731: ("POLL", "NONE", "5ee719bdadfc47f3", "255d058598cfb5aa"),
            28276: ("POLL", "NONE", "21e2283bfcabdc78", "ab2b35ccfda81299"),
            28549: ("POLL", "NONE", "0e104c7f68ada357", "ab2b35ccfda81299"),
            29048: ("POLL", "NONE", "cbe073ccb671a163", "ab2b35ccfda81299"),
            29049: ("POLL", "DOWN", "cbe073ccb671a163", "ab2b35ccfda81299"),
            29663: ("POLL", "NONE", "933fdcd7fa4d3768", "ab2b35ccfda81299"),
            31324: ("POLL", "NONE", "80fdb996f0bcece2", "16271bfb2c219c7e"),
            32140: ("POLL", "NONE", "f83d0adaaf494129", "16271bfb2c219c7e"),
            32181: ("POLL", "CONFIRM", "cace37aac4952f6a", "408294f96b2d2556"),
            32312: ("POLL", "NONE", "9a4177af6fb3f8b3", "ae41c92307478cc8"),
            36242: ("POLL", "NONE", "2bc57b5d47fb71d8", "ae41c92307478cc8"),
            36369: ("POLL", "DOWN", "7aa3440b272c278b", "0c4eaf7302bf8f76"),
            38254: ("POLL", "NONE", "4546d966f94b937a", "0c4eaf7302bf8f76"),
            39636: ("POLL", "NONE", "1a3eef7f2c74f33b", "0c4eaf7302bf8f76"),
            39902: ("POLL", "UP", "33c539280ea7b95e", "a9ededcfa134f463"),
            39903: ("POLL", "NONE", "2baacee4066be5a6", "a9ededcfa134f463"),
            39904: ("POLL", "UP", "2baacee4066be5a6", "a9ededcfa134f463"),
            40085: ("WAIT", "DOWN", "5b903cfeba46afbd", "a9ededcfa134f463"),
            40417: ("WAIT", "DOWN", "15a42a49832fd91e", "79a46449c06b7c74"),
            40801: ("POLL", "NONE", "7b7cb65189c2c1e1", "79a46449c06b7c74"),
            41585: ("POLL", "NONE", "f3c3271672165be5", "79a46449c06b7c74"),
            41819: ("POLL", "NONE", "7c324f59447e6d0f", "79a46449c06b7c74"),
            42177: ("POLL", "NONE", "5a84e211a40c02a2", "79a46449c06b7c74"),
            42433: ("POLL", "NONE", "83256237a1b57701", "79a46449c06b7c74"),
            42603: ("POLL", "NONE", "0bdfee144adabf2d", "79a46449c06b7c74"),
            43047: ("POLL", "NONE", "8acd3609698d8aa8", "79a46449c06b7c74"),
            43531: ("POLL", "NONE", "0df228bc73715309", "e907ea3385afd38b"),
            43552: ("POLL", "NONE", "b5c274c8fcffb919", "364d446679bea16e"),
            43554: ("POLL", "NONE", "5137460beb568530", "51e2adff0c2b9180"),
            43796: ("POLL", "NONE", "1ff928345100ff29", "51e2adff0c2b9180"),
            44019: ("POLL", "NONE", "eb9175a5c7deb865", "51e2adff0c2b9180"),
            44105: ("POLL", "NONE", "be78fd7ade9f1943", "51e2adff0c2b9180"),
            44234: ("POLL", "NONE", "5da8548e7f740f08", "51e2adff0c2b9180"),
            44414: ("POLL", "NONE", "20f67cb15da77ba7", "51e2adff0c2b9180"),
            44531: ("POLL", "NONE", "f129ada66f838914", "51e2adff0c2b9180"),
            44767: ("POLL", "NONE", "30c82f8becac536a", "51e2adff0c2b9180"),
            45013: ("POLL", "NONE", "25207af8a7a4da15", "51e2adff0c2b9180"),
            45238: ("POLL", "NONE", "3f88ecdc892d3f5f", "51e2adff0c2b9180"),
            45419: ("POLL", "NONE", "956bb18ee292887a", "51e2adff0c2b9180"),
            45642: ("POLL", "NONE", "2703c0e201306269", "51e2adff0c2b9180"),
            45784: ("POLL", "NONE", "47c284ce57d63b26", "51e2adff0c2b9180"),
            46033: ("POLL", "UP", "f20ed09fddeecb95", "14a787579f2246fb"),
            46097: ("POLL", "CONFIRM", "a1470fc5beda533a", "8d025995cf9a1176"),
            46098: ("POLL", "NONE", "d790e87038c0cb50", "38eed68eff44b534"),
            46150: ("POLL", "NONE", "df9799ea3d70a40c", "38eed68eff44b534"),
            46304: ("POLL", "NONE", "787585af50ddb01a", "38eed68eff44b534"),
            46541: ("POLL", "NONE", "cc0754b813f544b0", "38eed68eff44b534"),
            46788: ("POLL", "NONE", "aa63ca02a9b46c99", "38eed68eff44b534"),
            47121: ("POLL", "NONE", "ff44c7317f24882d", "38eed68eff44b534"),
            47388: ("POLL", "NONE", "5dae0683f63433a2", "38eed68eff44b534"),
            47792: ("POLL", "NONE", "13277706b525065d", "38eed68eff44b534"),
            48054: ("POLL", "NONE", "b02487fb9bc9869c", "28635308a61f9243"),
            48078: ("POLL", "CONFIRM", "8d88b21dbbfea8a7", "40d77a6c871f00f2"),
            48079: ("POLL", "NONE", "2a84a6a12e98626d", "0b3bf1871a52781c"),
            48080: ("POLL", "DOWN", "2a84a6a12e98626d", "0b3bf1871a52781c"),
            48641: ("POLL", "NONE", "36872a413a6875d3", "0b3bf1871a52781c"),
            48913: ("POLL", "NONE", "2c852a29b4173534", "0b3bf1871a52781c"),
            49178: ("POLL", "NONE", "ee7964dd8317b7fb", "0b3bf1871a52781c"),
            49514: ("POLL", "NONE", "461227ce4b487f58", "0b3bf1871a52781c"),
            49761: ("POLL", "NONE", "1058b4aa9b86354e", "0b3bf1871a52781c"),
            50058: ("POLL", "NONE", "c706eca289cf2be7", "0b3bf1871a52781c"),
            50313: ("POLL", "NONE", "4ae59c49e6eaae17", "0b3bf1871a52781c"),
            50476: ("POLL", "NONE", "130e043de5a0130a", "0b3bf1871a52781c"),
            50860: ("POLL", "NONE", "70dfec2dbee0b5be", "0b3bf1871a52781c"),
            51112: ("POLL", "NONE", "d464a3730e5c67a4", "0b3bf1871a52781c"),
            51251: ("POLL", "NONE", "46ab05e3e83f8cd1", "0b3bf1871a52781c"),
            51577: ("POLL", "NONE", "8de11113ed84b9fe", "0b3bf1871a52781c"),
            51927: ("POLL", "NONE", "a58df783d9332d52", "0b3bf1871a52781c"),
            52181: ("POLL", "NONE", "30d0f7168974a34b", "543ce510622d7e8b"),
            52221: ("POLL", "NONE", "5a28a05a48003d6e", "543ce510622d7e8b"),
            52645: ("POLL", "NONE", "ce6d8a6658feec82", "543ce510622d7e8b"),
            52688: ("POLL", "CONFIRM", "a0369a867990524c", "17786d15b4960eda"),
            52689: ("POLL", "NONE", "4093506091495014", "400cf877cf5f2b1f"),
            52728: ("POLL", "NONE", "569286e872ac1a08", "400cf877cf5f2b1f"),
            53152: ("POLL", "NONE", "cfb2eea74ae2bacc", "400cf877cf5f2b1f"),
            53437: ("POLL", "NONE", "e4767c0fffa85be7", "400cf877cf5f2b1f"),
            53574: ("POLL", "NONE", "8d1d1d6e983b28a8", "91be609a29c61b1d"),
            53578: ("POLL", "NONE", "01736596ab541f2d", "91be609a29c61b1d"),
            53579: ("POLL", "QUIT", "01736596ab541f2d", "91be609a29c61b1d"),
        }
        if len(checkpoints) != 53580:
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
            8325: (136, 65, 99, 3),
            8367: (140, 32, 114, 3),
            8415: (140, 46, 82, 3),
            8501: (174, 42, 114, 6),
            8513: (156, 50, 72, 0),
            8556: (138, 82, 98, 0),
            8591: (138, 82, 130, 0),
            8655: (130, 45, 164, 3),
            8671: (130, 45, 148, 3),
            8777: (130, 50, 55, 3),
            8778: (130, 50, 55, 3),
            8782: (130, 50, 55, 3),
            8810: (130, 50, 83, 0),
            8902: (130, 46, 167, 0),
            8911: (116, 114, 157, 0),
            8956: (176, 53, 177, 3),
            9023: (180, 51, 116, 3),
            9054: (180, 38, 132, 0),
            9099: (184, 120, 132, 0),
            9132: (184, 108, 153, 6),
            9136: (184, 108, 153, 6),
            9240: (184, 91, 104, 9),
            9244: (184, 91, 104, 9),
            9313: (192, 41, 70, 3),
            9321: (192, 41, 70, 3),
            9332: (192, 41, 70, 3),
            9340: (192, 41, 70, 3),
            9351: (192, 41, 70, 3),
            9377: (192, 47, 50, 3),
            9411: (192, 47, 50, 3),
            9412: (192, 47, 50, 3),
            9413: (192, 47, 50, 3),
            9469: (448, 56, 124, 0),
            9505: (448, 62, 117, 3),
            9533: (448, 62, 117, 3),
            9657: (442, 155, 53, 3),
            9698: (442, 155, 53, 3),
            9800: (442, 150, 14, 3),
            9828: (442, 150, 14, 3),
            9832: (198, 71, 70, 3),
            9866: (198, 71, 36, 3),
            9891: (198, 71, 36, 3),
            9922: (198, 71, 36, 3),
            9959: (196, 150, 15, 0),
            10030: (196, 135, 37, 0),
            10031: (196, 135, 37, 0),
            10138: (444, 95, 90, 6),
            10139: (444, 95, 90, 6),
            10140: (444, 95, 90, 6),
            10262: (444, 85, 162, 6),
            10273: (190, 112, 71, 0),
            10311: (190, 112, 71, 0),
            10420: (190, 105, 115, 6),
            10421: (190, 105, 115, 6),
            10539: (190, 88, 151, 9),
            10599: (186, 51, 158, 3),
            10664: (186, 38, 135, 3),
            10701: (182, 53, 111, 0),
            10769: (178, 84, 157, 0),
            10770: (178, 84, 158, 0),
            10840: (178, 56, 121, 6),
            11001: (178, 62, 38, 6),
            11060: (212, 40, 86, 3),
            11083: (212, 37, 68, 9),
            11108: (214, 41, 35, 0),
            11125: (214, 45, 20, 3),
            11126: (216, 89, 167, 3),
            11168: (220, 30, 36, 3),
            11195: (220, 36, 15, 3),
            11197: (220, 36, 15, 3),
            11311: (220, 36, 15, 3),
            11312: (220, 36, 15, 3),
            11324: (220, 36, 15, 3),
            11325: (220, 36, 15, 3),
            11354: (222, 89, 126, 0),
            11402: (224, 114, 29, 0),
            11459: (224, 160, 40, 0),
            11568: (224, 160, 40, 0),
            11585: (228, 153, 150, 0),
            11588: (228, 153, 152, 0),
            11694: (228, 77, 162, 6),
            11698: (228, 77, 162, 6),
            11800: (228, 100, 115, 9),
            11804: (228, 100, 115, 9),
            11867: (232, 153, 25, 0),
            11869: (232, 153, 27, 0),
            11923: (232, 125, 49, 3),
            11930: (236, 36, 55, 3),
            11962: (236, 51, 38, 9),
            12023: (236, 20, 51, 9),
            12029: (236, 20, 49, 9),
            12095: (236, 43, 10, 9),
            12165: (240, 108, 9, 9),
            12221: (240, 115, 40, 6),
            12250: (240, 129, 51, 9),
            12363: (244, 211, 11, 6),
            12366: (244, 212, 11, 9),
            12391: (244, 204, 25, 6),
            12485: (252, 39, 57, 3),
            12513: (252, 39, 57, 3),
            12528: (254, 195, 52, 0),
            12646: (246, 150, 9, 0),
            12741: (242, 18, 10, 0),
            12827: (238, 123, 49, 0),
            12891: (234, 153, 123, 3),
            13148: (230, 161, 54, 3),
            13225: (226, 56, 119, 3),
            13272: (220, 30, 36, 3),
            13300: (220, 38, 16, 3),
            13334: (222, 89, 126, 0),
            13382: (224, 114, 29, 0),
            13456: (228, 153, 150, 0),
            13709: (232, 153, 25, 0),
            13768: (236, 36, 55, 3),
            13860: (240, 107, 9, 3),
            13959: (244, 226, 15, 3),
            14076: (252, 28, 59, 3),
            14117: (250, 241, 9, 0),
            14129: (256, 28, 97, 3),
            14250: (262, 143, 144, 0),
            14251: (260, 31, 14, 0),
            14252: (262, 143, 144, 0),
            14286: (272, 75, 150, 3),
            14391: (364, 35, 17, 3),
            14448: (366, 77, 87, 0),
            14520: (280, 36, 11, 9),
            14527: (284, 137, 149, 0),
            14925: (284, 14, 162, 6),
            14928: (284, 14, 162, 6),
            14974: (284, 44, 146, 3),
            15074: (284, 24, 82, 3),
            15186: (284, 72, 22, 9),
            15234: (284, 96, 8, 3),
            15324: (284, 139, 25, 3),
            15326: (376, 140, 25, 3),
            15331: (376, 140, 30, 0),
            15467: (376, 59, 19, 6),
            15600: (376, 18, 107, 0),
            15710: (376, 73, 158, 0),
            15821: (376, 152, 150, 3),
            15955: (376, 119, 151, 9),
            15988: (286, 40, 10, 3),
            16008: (282, 112, 103, 0),
            16065: (364, 30, 34, 3),
            16087: (364, 35, 17, 3),
            16100: (364, 35, 17, 3),
            16102: (364, 35, 17, 3),
            16126: (366, 77, 87, 0),
            16179: (274, 125, 159, 0),
            16256: (288, 116, 26, 6),
            16341: (294, 60, 131, 6),
            16449: (336, 53, 173, 3),
            16585: (340, 92, 177, 3),
            16835: (342, 92, 77, 0),
            16846: (342, 93, 84, 3),
            16974: (338, 110, 101, 0),
            17060: (264, 57, 19, 6),
            17109: (264, 13, 16, 6),
            17113: (268, 13, 170, 3),
            17369: (296, 165, 148, 3),
            17443: (300, 10, 83, 3),
            17511: (304, 4, 22, 0),
            17560: (308, 110, 81, 3),
            17699: (312, 55, 45, 3),
            17855: (316, 6, 20, 0),
            17891: (320, 25, 176, 3),
            18160: (324, 158, 12, 0),
            18211: (328, 80, 22, 0),
            18235: (332, 144, 172, 3),
            18316: (590, 145, 126, 0),
            18375: (334, 97, 16, 0),
            18456: (326, 122, 49, 3),
            18721: (322, 41, 20, 0),
            18757: (318, 93, 28, 3),
            18915: (314, 132, 20, 0),
            19053: (310, 50, 22, 0),
            19102: (306, 63, 93, 3),
            19172: (302, 133, 109, 0),
            19246: (298, 110, 164, 0),
            19504: (270, 10, 16, 0),
            19555: (266, 38, 94, 0),
            19703: (422, 43, 59, 3),
            19742: (426, 145, 144, 3),
            19764: (430, 147, 108, 3),
            19773: (676, 146, 99, 3),
            19853: (424, 144, 111, 0),
            20105: (434, 146, 170, 3),
            21068: (438, 25, 30, 3),
            21120: (438, 25, 26, 3),
            21200: (450, 59, 66, 9),
            21369: (458, 37, 17, 9),
            21535: (462, 134, 128, 3),
            21698: (466, 99, 122, 3),
            21785: (466, 154, 90, 3),
            21786: (466, 155, 90, 9),
            21929: (466, 114, 31, 0),
            21930: (466, 115, 31, 9),
            22065: (466, 65, 72, 3),
            22066: (466, 65, 71, 3),
            22067: (466, 82, 49, 3),
            22187: (466, 82, 49, 3),
            24387: (340, 91, 28, 3),
            24696: (338, 110, 101, 0),
            24882: (470, 43, 50, 3),
            25049: (482, 57, 18, 9),
            25340: (498, 11, 13, 0),
            25416: (510, 44, 22, 0),
            25721: (678, 25, 17, 3),
            26729: (678, 25, 17, 3),
            27267: (606, 166, 19, 0),
            27731: (584, 25, 27, 3),
            28276: (632, 107, 156, 0),
            28549: (634, 48, 127, 6),
            28862: (506, 70, 12, 0),
            28883: (636, 39, 10, 0),
            28991: (642, 29, 75, 0),
            29034: (646, 43, 38, 0),
            29048: (650, 26, 164, 0),
            29049: (650, 26, 164, 0),
            29663: (652, 53, 177, 3),
            31037: (664, 25, 11, 0),
            31324: (668, 25, 28, 0),
            32140: (672, 143, 41, 0),
            32181: (658, 53, 123, 0),
            32312: (654, 134, 93, 0),
            36242: (756, 31, 147, 9),
            36370: (758, 9, 105, 0),
            38254: (730, 44, 4, 0),
            39636: (726, 115, 61, 0),
            39902: (720, 131, 173, 3),
            39903: (720, 131, 172, 3),
            39904: (720, 131, 172, 3),
            40085: (720, 73, 139, 3),
            40417: (720, 153, 54, 3),
            40801: (762, 166, 45, 0),
            41585: (800, 63, 195, 3),
            41819: (804, 50, 138, 3),
            42177: (808, 30, 49, 3),
            42433: (816, 107, 160, 3),
            42603: (820, 79, 74, 3),
            43047: (824, 87, 27, 3),
            43531: (824, 76, 82, 3),
            43552: (824, 79, 77, 3),
            43554: (824, 79, 77, 3),
            43796: (826, 87, 35, 0),
            44019: (822, 80, 193, 0),
            44105: (818, 107, 57, 0),
            44234: (810, 30, 168, 0),
            44414: (806, 50, 140, 0),
            44531: (802, 136, 124, 0),
            44767: (828, 63, 195, 3),
            45013: (832, 49, 28, 3),
            45238: (836, 106, 163, 0),
            45419: (840, 57, 83, 0),
            45642: (844, 48, 146, 0),
            45784: (848, 85, 76, 0),
            46033: (848, 54, 39, 3),
            46098: (848, 86, 28, 3),
            46150: (834, 63, 133, 0),
            46304: (830, 23, 45, 0),
            46541: (856, 63, 195, 3),
            46788: (860, 89, 30, 0),
            47121: (864, 52, 147, 0),
            47388: (868, 99, 57, 0),
            47792: (872, 29, 51, 0),
            48054: (872, 75, 37, 3),
            48079: (872, 71, 27, 3),
            48080: (872, 71, 27, 3),
            48641: (874, 29, 51, 0),
            48913: (870, 99, 160, 3),
            49178: (866, 51, 28, 3),
            49514: (862, 89, 135, 0),
            49761: (858, 59, 124, 0),
            50058: (880, 67, 195, 3),
            50313: (884, 97, 65, 3),
            50476: (888, 80, 161, 0),
            50860: (892, 100, 65, 3),
            51112: (896, 31, 163, 0),
            51251: (900, 86, 28, 3),
            51577: (904, 29, 49, 3),
            51927: (908, 86, 185, 3),
            52181: (908, 66, 150, 3),
            52221: (912, 88, 31, 3),
            52645: (916, 48, 150, 0),
            52689: (916, 48, 159, 3),
            52728: (918, 48, 31, 3),
            53152: (914, 88, 150, 0),
            53437: (910, 86, 82, 0),
            53574: (910, 64, 88, 0),
            53578: (920, 36, 51, 0),
            53579: (920, 36, 51, 0),
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
        if u16(save, 0x424) != 920:
            raise ValueError("continuous checkpoint is not post-gate directory 920")
        world_x = u16(save, 0x41B) + ((u16(save, 0x012) + 2) >> 1)
        world_y = u16(save, 0x41D) + ((u16(save, 0x02A) + 16) >> 3)
        if (world_x, world_y) != (36, 51):
            raise ValueError(f"post-gate position is {(world_x, world_y)!r}")
        if u16(save, 0x51C) != 0:
            raise ValueError("RPG did not consume and clear the post-battle entity continuation")
        if u16(save, 0x10) != 4 or u16(save, 0x104) != 1163:
            raise ValueError("post-east-T9 party count or money differs")
        if u16(save, 0x49C) != 0x1900:
            raise ValueError("post-north-T9 FIG random cursor differs")
        if save[0x529] != 1:
            raise ValueError("AREA2 Jianmu return portal did not set travel flag 11")
        # MAP0 action 4011h sets bit 0010h before event 334; FIR3 action
        # 400ah later adds bit 0001h before event 52. Opcode 3 hides
        # directory-252 entity one, opcode 34 redirects the location-220 mage
        # to event 336, and opcode 58 executes fixed ORC directory 802ch. The
        # legal default-command trace ends in defeat; event 334 contains no
        # victory/defeat branch and OC resumes at this same world position.
        if u16(save, 0x51A) != 0x01FF:
            raise ValueError(
                    "Jianmu/Taotie/FIR3/SD01/T9 MAP0 once-only trigger flags differ")
        expected_party = [
            (0x2000, 0, 156, 122, 122, 94, 94, 328, 843),
            (0x2000, 0, 150, 46, 46, 99, 99, 328, 842),
            (0x2000, 0, 161, 140, 140, 46, 46, 600, 847),
            (0x3200, 0, 153, 50, 50, 94, 94, 263, 838),
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
                    f"post-north-T9 actor {actor} state is {actual!r}, "
                    f"expected {expected!r}")
        if [u16(save, 0x106 + actor * 0x9F + 0x31)
                for actor in range(4)] != [17, 17, 17, 17]:
            raise ValueError("post-north-T9 party levels differ")
        expected_inventory = [
            262, 206, 110, 206, 291, 104, 119, 257, 83,
            89, 278, 250, 281, 259,
        ] + [0] * 36
        actual_inventory = [u16(save, 0x382 + slot * 2) for slot in range(50)]
        if actual_inventory != expected_inventory:
            raise ValueError("post-north-T9 inventory/compaction differs")

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
        if map_field(image, 220, 9, 0) != 340:
            raise ValueError("event 336 did not persist the mage follow-up event 340")
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
            raise ValueError("mainline checkpoint did not stop at explicit world quit")
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
        "T9 north item 259 -> four-treasure gate)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
