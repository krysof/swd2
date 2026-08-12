#!/usr/bin/env python3
"""Fail closed on the shipped captured-ally visual ability domain."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED_EFFECTS = {
    0x32: {54, 127}, 0x33: {52}, 0x35: {59, 125}, 0x37: {73},
    0x38: {76, 135}, 0x39: {78}, 0x3A: {77}, 0x3D: {53},
    0x3E: {67}, 0x40: {57}, 0x41: {70}, 0x42: {74},
    0x43: {87, 115}, 0x44: {89}, 0x45: {91, 118, 123},
    0x46: {95, 130}, 0x48: {84, 116, 128}, 0x49: {5, 31},
    0x4A: {11}, 0x4C: {10, 120, 124}, 0x4E: {1}, 0x54: {131},
    0x57: {12}, 0x58: {28, 133}, 0x59: {29}, 0x5A: {16},
    0x5B: {39, 146}, 0x5D: {30}, 0x5E: {6, 65, 114, 122, 134},
    0x5F: {4, 72}, 0x64: {32, 56, 117, 132},
    0x65: {58, 148}, 0x67: {38}, 0x69: {37},
}
REQUIRED_MEDIA = {
    0x32: 0x80, 0x34: 0x80, 0x35: 0x80, 0x36: 0x80, 0x40: 0x80,
    0x37: 0x40, 0x43: 0x40, 0x45: 0x20, 0x48: 0x20,
}
ARCHETYPE_REFERENCES = {
    "fig-ally-ability-rgb-reference.json":
        "original_fig_captured_ally_offensive_ability",
    "fig-ally-multitarget-rgb-reference.json":
        "original_fig_captured_ally_multitarget_ability",
    "fig-ally-special-ability-rgb-reference.json":
        "original_fig_captured_ally_special_ability",
    "fig-ally-medium-rgb-reference.json":
        "original_fig_captured_ally_medium_install",
    "fig-ally-medium-dismiss-rgb-reference.json":
        "original_fig_captured_ally_medium_dismissal",
    "fig-ally-player-buff-rgb-reference.json":
        "original_fig_captured_ally_player_buff",
    "fig-ally-player-ward-rgb-reference.json":
        "original_fig_captured_ally_player_ward",
    "fig-ally-missing-medium-rgb-reference.json":
        "original_fig_captured_ally_missing_medium",
}


def word(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("word lies outside executable data")
    return int.from_bytes(data[offset:offset + 2], "little")


def mz_image(path: Path) -> bytes:
    executable = path.read_bytes()
    if executable[:2] != b"MZ" or len(executable) < 0x1C:
        raise ValueError(f"{path.name} is not a DOS executable")
    header = word(executable, 8) * 16
    if header < 0x1C or header > len(executable):
        raise ValueError(f"{path.name} has a malformed MZ header")
    return executable[header:]


def archive_records(image: bytes) -> list[bytes]:
    directory_bytes = word(image, 2)
    sentinel = word(image, 0)
    if directory_bytes < 4 or directory_bytes > len(image) or \
            directory_bytes % 2:
        raise ValueError("ITEM.EXE directory is malformed")
    offsets = [word(image, offset)
               for offset in range(0, directory_bytes, 2)]
    result = []
    for start in offsets:
        finish = min((value for value in offsets if value > start),
                     default=sentinel)
        if start > finish or finish > len(image):
            raise ValueError("ITEM.EXE record bounds are malformed")
        result.append(image[start:finish] if start < finish else b"")
    return result


def exact_rgb_pairs(value: object):
    if isinstance(value, dict):
        rewrite = value.get("rewrite_rgb_sha256")
        original = value.get("original_rgb_sha256")
        if isinstance(rewrite, str) and rewrite == original:
            yield rewrite
        rewrite_crop = value.get("rewrite_crop_rgb_sha256")
        original_crop = value.get("original_crop_rgb_sha256")
        if isinstance(rewrite_crop, str) and rewrite_crop == original_crop:
            yield rewrite_crop
        for child in value.values():
            yield from exact_rgb_pairs(child)
    elif isinstance(value, list):
        for child in value:
            yield from exact_rgb_pairs(child)


def valid_digest(value: str) -> bool:
    return len(value) == 64 and all(char in "0123456789abcdef" for char in value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    parser.add_argument("evidence", type=Path, nargs="?", default=root / "scripts")
    args = parser.parse_args()
    try:
        records = archive_records(mz_image(args.game / "ITEM.EXE"))
        summonable = {}
        for item_id in range(len(records) - 2):
            record = records[item_id + 2]
            if item_id >= 0x13A and len(record) >= 0x50 and \
                    (word(record, 5) & 2):
                summonable[item_id] = tuple(
                    word(record, offset)
                    for offset in (0x32, 0x3A, 0x42, 0x46))
        if set(summonable) != set(range(314, 504)):
            raise ValueError(
                f"summonable ITEM domain changed: {sorted(summonable)}")

        fig = mz_image(args.game / "FIG.EXE")
        if fig[:1] != b"\xB8" or fig[3:5] != b"\x8E\xD8":
            raise ValueError("FIG.EXE data-segment prologue changed")
        table = word(fig, 1) * 16 + 0x1DCC
        abilities = {
            ability for item_abilities in summonable.values()
            for ability in item_abilities if ability
        }
        visual = {}
        for ability in abilities:
            if ability >= 151:
                raise ValueError(f"captured-ally ability exceeds FIG table: {ability}")
            record = fig[table + ability * 20:table + (ability + 1) * 20]
            effect = word(record, 14)
            if effect > 0x30:
                visual[ability] = (word(record, 12), effect)
        derived_effects: dict[int, set[int]] = {}
        for ability, (_, effect) in visual.items():
            derived_effects.setdefault(effect, set()).add(ability)
        if derived_effects != EXPECTED_EFFECTS or len(abilities) != 68 or \
                len(visual) != 57 or len(derived_effects) != 34:
            raise ValueError(
                "captured-ally visual domain changed: "
                f"abilities={len(abilities)}, visual={len(visual)}, "
                f"effects={len(derived_effects)}")

        missing_flags = set()
        for ability, (flags, effect) in visual.items():
            required = REQUIRED_MEDIA.get(effect)
            if required is not None and not (flags & required):
                missing_flags.add((ability, effect, flags, required))
        if missing_flags != {(116, 0x48, 0xA100, 0x20)}:
            raise ValueError(
                f"captured-ally required-medium exceptions changed: {missing_flags}")

        player_handler_effects = set()
        for path in sorted(
                args.evidence.glob("fig-player-effect*-rgb-reference.json")):
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("kind") == "original_fig_player_effect" and \
                    isinstance(payload.get("effect_code"), int):
                player_handler_effects.add(payload["effect_code"])
        for filename in (
                "fig-player-damage-rgb-reference.json",
                "fig-player-dispel-rgb-reference.json",
                "fig-player-barrier-rgb-reference.json",
                "fig-flagged-medium-rgb-reference.json"):
            payload = json.loads(
                (args.evidence / filename).read_text(encoding="utf-8"))
            player_handler_effects.add(payload["effect_code"])
        expected_handlers = {
            *range(0x32, 0x3B), *range(0x40, 0x47), *range(0x48, 0x66),
        }
        if player_handler_effects != expected_handlers:
            raise ValueError("player visual-handler evidence domain changed")
        offensive_effects = set(derived_effects) - {0x3D, 0x3E, 0x67, 0x69}
        if not offensive_effects <= player_handler_effects:
            raise ValueError("captured-ally offensive effect lacks handler evidence")

        exact_pairs = 0
        for filename, kind in ARCHETYPE_REFERENCES.items():
            path = args.evidence / filename
            payload = json.loads(path.read_text(encoding="utf-8"))
            pairs = list(exact_rgb_pairs(payload))
            if payload.get("kind") != kind or not pairs or any(
                    not valid_digest(value) for value in pairs):
                raise ValueError(f"captured-ally archetype evidence differs: {filename}")
            exact_pairs += len(pairs)
        missing = json.loads((
            args.evidence / "fig-ally-missing-medium-rgb-reference.json"
        ).read_text(encoding="utf-8"))
        if missing.get("captured_item_id") != 402 or \
                missing.get("captured_ally_ability_id") != 116 or \
                missing.get("effect_code") != 0x48 or \
                missing.get("target_flags") != 0xA100 or \
                len(missing.get("matched_frames", [])) != 44:
            raise ValueError("ability 116 missing-medium evidence boundary changed")

        domain_digest = hashlib.sha256(";".join(
            f"{effect:02x}:" + ",".join(map(str, sorted(ids)))
            for effect, ids in sorted(derived_effects.items())
        ).encode("ascii")).hexdigest()
        if domain_digest != \
                "b98dbe6287d2e0137b274cf1717e77ab4c2bed57555930de68d8880c89afa872":
            raise ValueError(f"captured-ally domain digest changed: {domain_digest}")

        print(
            "FIG summonable visual-domain audit: 190 item definitions, "
            "68 referenced abilities, 57 visual abilities / 34 effects; "
            "the sole unflagged required-medium route is item 402 / "
            f"ability 116 and all 8 route archetypes contain exact RGB pairs "
            f"({exact_pairs} registered pairs across the archetype set)"
        )
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG summonable visual-domain audit: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
