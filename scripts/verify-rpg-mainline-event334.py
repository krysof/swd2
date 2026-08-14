#!/usr/bin/env python3
"""Lock a continuous legal route through Jianmu event 334 and ORC 802ch."""

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
        # return/mine encounters, fixed Taotie encounter 16, then twenty-one
        # legal post-Taotie encounters through the sixth post-Xirang return
        # fight, the two overworld encounters on the road to the temple, and
        # fixed Jianmu encounter 14, the eastern TREE encounter, two released
        # random encounters on the first ascent, and the additional encounter
        # naturally triggered while descending into the treasure route.
        for index in range(74):
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
            ("input", "total"): 12514,
            ("input", "consumed"): 12514,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 1603,
            ("boundaries", "poll"): 10947,
            ("boundaries", "text"): 945,
            ("video", "frames"): 36738,
            ("video", "direct_updates"): 5639,
            ("video", "fnv1a64"): "59d1ce0ced81ac27",
            ("audio", "music_calls"): 339,
            ("audio", "voice_calls"): 721,
            ("audio", "stop_music_calls"): 9,
            ("audio", "stop_audio_calls"): 152,
            ("audio", "fnv1a64"): "066176bbbded8350",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 1639035:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "79cbe260b2fb6ce4",
            "mapz_fnv1a64": "4692decfa52e471d",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 36738 or frames[-1] != "51c92fd1c6310e61":
            raise ValueError("mainline post-event-334 frame differs")

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
            12513: ("POLL", "QUIT", "79cbe260b2fb6ce4", "4692decfa52e471d"),
        }
        if len(checkpoints) != 12514:
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
        if u16(save, 0x49C) != 0x13C4:
            raise ValueError("Jianmu directory-252 FIG random cursor differs")
        # MAP0 action 4011h sets bit 0010h before event 334. Opcode 3 hides
        # directory-252 entity one, opcode 34 redirects the location-220 mage
        # to event 336, and opcode 58 executes fixed ORC directory 802ch. The
        # legal default-command trace ends in defeat; event 334 contains no
        # victory/defeat branch and OC resumes at this same world position.
        if u16(save, 0x51A) != 0x0014:
            raise ValueError("Jianmu/Taotie MAP0 once-only trigger flags differ")
        expected_party = [
            (0x2000, 0, 134, 110, 110, 1, 84, 707, 717),
            (0x2000, 0, 138, 42, 42, 5, 91, 696, 706),
            (0x2000, 0, 161, 109, 140, 46, 46, 262, 847),
            (0x3000, 0, 140, 46, 46, 7, 84, 627, 702),
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
        if map_field(image, 220, 9, 0) != 336:
            raise ValueError("event 334 did not persist the mage redirect to 336")
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
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("mainline checkpoint did not stop at explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline prefix validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RPG mainline prefix validation: OK "
        "(new game -> released mainline -> Jianmu event 334 -> fixed ORC 802ch legal defeat -> OC world)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
