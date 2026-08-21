#!/usr/bin/env python3
"""Audit a title-loaded untouched RPG probe against modern boundary 5."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
PATH_FIELDS = ((0x42D, 22), (0x443, 22), (0x459, 22),
               (0x46F, 22), (0x485, 24))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fnv1a64(data: bytes) -> str:
    digest = FNV_OFFSET
    for value in data:
        digest ^= value
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def u16(data: bytes, offset: int) -> int:
    return data[offset] | data[offset + 1] << 8


def world(data: bytes) -> dict[str, int]:
    return {
        "x": u16(data, 0x41B) + ((u16(data, 0x12) + 2) >> 1),
        "y": u16(data, 0x41D) + ((u16(data, 0x2A) + 16) >> 3),
    }


def mapz_field_offset(data: bytes, location_offset: int, field: int,
                      entity: int) -> tuple[int, int, int]:
    """Return file offset, area-image offset and entity count for a word."""
    header = u16(data, 8) * 16
    if header < 28 or header + 4 > len(data):
        raise ValueError("MAPZ MZ wrapper is malformed")
    image = data[header:]
    if location_offset < 0 or location_offset + 2 > len(image):
        raise ValueError("MAPZ location directory offset is invalid")
    location = u16(image, location_offset)
    if location + 28 > len(image):
        raise ValueError("MAPZ location record is truncated")
    area = u16(image, location + 26)
    if area + 6 > len(image):
        raise ValueError("MAPZ area record is truncated")
    count = u16(image, area + 4)
    if field < 0 or field >= 11 or entity < 0 or entity >= count:
        raise ValueError("MAPZ field/entity selector is invalid")
    image_offset = area + 6 + (field * count + entity) * 2
    if image_offset + 2 > len(image):
        raise ValueError("MAPZ entity word is truncated")
    return header + image_offset, area, count


def event_stream(executable: bytes, directory_offset: int) -> bytes:
    """Read an aligned, ffff-terminated released CHNA event stream."""
    if len(executable) < 28 or executable[:2] != b"MZ":
        raise ValueError("released CHNA1.EXE has no MZ wrapper")
    image_start = u16(executable, 8) * 16
    image = executable[image_start:]
    if (directory_offset & 1) != 0 or directory_offset + 2 > len(image):
        raise ValueError("released CHNA event directory offset is invalid")
    start = u16(image, directory_offset)
    for cursor in range(start, len(image) - 1, 2):
        if image[cursor:cursor + 2] == b"\xff\xff":
            return image[start:cursor + 2]
    raise ValueError("released CHNA event has no aligned terminator")


def artifact(root: Path, value: object) -> bytes:
    if not isinstance(value, dict) or not isinstance(value.get("path"), str):
        raise ValueError("boundary artifact is malformed")
    path = root / value["path"]
    data = path.read_bytes()
    if value.get("bytes") != len(data) or value.get("fnv1a64") != fnv1a64(data):
        raise ValueError(f"boundary artifact differs: {value.get('path')}")
    return data


def reconstructed_original(modern: bytes, entries: object, label: str) -> bytes:
    if not isinstance(entries, list):
        raise ValueError(f"declared {label} differences are malformed")
    result = bytearray(modern)
    previous = -1
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError(f"declared {label} difference is malformed")
        try:
            offset = int(entry["offset"], 16)
            original = entry["original"]
            expected_modern = entry["modern"]
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(
                f"declared {label} difference is malformed") from error
        if offset <= previous or offset >= len(result):
            raise ValueError(f"declared {label} offsets are not strictly ordered")
        if not isinstance(original, int) or not 0 <= original <= 0xFF or \
                not isinstance(expected_modern, int) or \
                not 0 <= expected_modern <= 0xFF:
            raise ValueError(f"declared {label} byte is invalid")
        if result[offset] != expected_modern or original == expected_modern:
            raise ValueError(
                f"declared {label} difference no longer applies at {offset:#x}")
        result[offset] = original
        previous = offset
    return bytes(result)


def validate_digest_set(data: bytes, values: object, prefix: str) -> None:
    if not isinstance(values, dict) or \
            values.get(f"{prefix}_fnv1a64") != fnv1a64(data) or \
            values.get(f"{prefix}_sha256") != sha256(data):
        raise ValueError(f"{prefix} digest differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("boundaries", type=Path)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        manifest = json.loads(
            (args.boundaries / "manifest.json").read_text(encoding="utf-8"))
        if (reference.get("schema_version"), reference.get("kind"),
                reference.get("status")) != (
                    1, "original_rpg_title_loaded_boundary_probe",
                    "partial_probe_with_declared_differences"):
            raise ValueError("original RPG boundary reference schema differs")
        limitation = reference.get("limitation")
        if not isinstance(limitation, str) or \
                "not a byte-exact checkpoint" not in limitation or \
                "uninterrupted original mainline" not in limitation or \
                "does not verify the full-playthrough gate" not in limitation:
            raise ValueError("partial-probe limitation is missing")
        if manifest.get("status") != "complete":
            raise ValueError("modern module boundary capture is incomplete")

        index = reference.get("modern_invocation_index")
        invocations = manifest.get("invocations")
        if not isinstance(index, int) or not isinstance(invocations, list) or \
                index < 0 or index >= len(invocations):
            raise ValueError("modern invocation index is invalid")
        invocation = invocations[index]
        expected_header = (
            reference.get("modern_module"), reference.get("input_marker"),
            reference.get("output_marker"), reference.get("input_range"))
        actual_header = (
            invocation.get("module"), invocation.get("input_marker"),
            invocation.get("output_marker"), invocation.get("input_range"))
        if actual_header != expected_header:
            raise ValueError("modern RPG invocation identity/range differs")
        begin = invocation["input_range"]["begin"]
        end = invocation["input_range"]["end"]
        if manifest.get("input_steps", [])[begin:end] != reference.get("input_steps"):
            raise ValueError("modern RPG input slice differs")

        entry = invocation.get("entry")
        leave = invocation.get("exit")
        if not isinstance(entry, dict) or not isinstance(leave, dict):
            raise ValueError("modern RPG boundary snapshots are missing")
        entry_state = artifact(args.boundaries, entry.get("state"))
        entry_transfer = artifact(args.boundaries, entry.get("transfer"))
        entry_mapz = artifact(args.boundaries, entry.get("mapz"))
        entry_name = artifact(args.boundaries, entry.get("name"))
        modern_state = artifact(args.boundaries, leave.get("state"))
        modern_transfer = artifact(args.boundaries, leave.get("transfer"))
        modern_mapz = artifact(args.boundaries, leave.get("mapz"))
        modern_name = artifact(args.boundaries, leave.get("name"))
        if fnv1a64(entry_state) != reference.get("entry_state_fnv1a64") or \
                sha256(entry_state) != reference.get("entry_state_sha256") or \
                sha256(entry_transfer) != reference.get("entry_transfer_sha256"):
            raise ValueError("modern RPG entry digest differs")
        if entry_transfer != b"OC" + entry_state or \
                modern_transfer != b"IF" + modern_state:
            raise ValueError("modern SharedTransfer bytes differ from DOS ABI")

        modern = reference.get("modern")
        validate_digest_set(modern_state, modern, "output_state")
        validate_digest_set(modern_mapz, modern, "mapz")
        validate_digest_set(modern_name, modern, "name")
        if not isinstance(modern, dict) or \
                modern.get("output_transfer_sha256") != sha256(modern_transfer) or \
                modern.get("world") != world(modern_state) or \
                modern.get("battle_encounter_offset") != u16(modern_state, 0x4A0) or \
                modern.get("map_location_directory_offset") != u16(modern_state, 0x424) or \
                modern.get("rpg_load_cursor") != u16(modern_state, 0x49C) or \
                modern.get("persisted_map_entity_base") != u16(modern_state, 0x429):
            raise ValueError("modern RPG semantic summary differs")

        original_state = reconstructed_original(
            modern_state, reference.get("declared_state_byte_differences"),
            "state")
        original_mapz = reconstructed_original(
            modern_mapz, reference.get("declared_mapz_byte_differences"),
            "MAPZ")
        original = reference.get("original")
        validate_digest_set(original_state, original, "output_state")
        validate_digest_set(original_mapz, original, "mapz")
        validate_digest_set(modern_name, original, "name")
        original_transfer = b"IF" + original_state
        if not isinstance(original, dict) or \
                original.get("output_transfer_sha256") != sha256(original_transfer) or \
                original.get("world") != world(original_state) or \
                original.get("battle_encounter_offset") != u16(original_state, 0x4A0) or \
                original.get("map_location_directory_offset") != u16(original_state, 0x424) or \
                original.get("rpg_load_cursor") != u16(original_state, 0x49C) or \
                original.get("persisted_map_entity_base") != u16(original_state, 0x429):
            raise ValueError("untouched RPG semantic summary differs")

        invariants = reference.get("exact_shared_invariants")
        invariant_tuple = (
            "IF", len(original_state), len(original_mapz), len(modern_name),
            u16(original_state, 0x4A0), u16(original_state, 0x424),
            u16(original_state, 0x429), "E:", True, True)
        if not isinstance(invariants, dict) or (
                invariants.get("transfer_marker"), invariants.get("state_bytes"),
                invariants.get("mapz_bytes"), invariants.get("name_bytes"),
                invariants.get("battle_encounter_offset"),
                invariants.get("map_location_directory_offset"),
                invariants.get("persisted_map_entity_base"),
                invariants.get("persisted_path_drive"),
                invariants.get("mapz_exact"), invariants.get("name_exact")) != \
                invariant_tuple:
            raise ValueError("shared semantic invariant summary differs")
        if modern_mapz != original_mapz or entry_name != modern_name or \
                u16(modern_state, 0x4A0) != u16(original_state, 0x4A0) or \
                u16(modern_state, 0x424) != u16(original_state, 0x424) or \
                u16(entry_state, 0x429) != u16(modern_state, 0x429) or \
                u16(modern_state, 0x429) != u16(original_state, 0x429):
            raise ValueError("declared exact shared invariant differs")
        for state in (entry_state, modern_state, original_state):
            for offset, size in PATH_FIELDS:
                path = state[offset:offset + size].split(b"\0", 1)[0]
                if path and not path.startswith(b"E:"):
                    raise ValueError(
                        f"persisted resource path lacks E: at {offset:#x}: {path!r}")

        explained = reference.get("explained_differences")
        cursor = explained.get("rpg_load_cursor") \
            if isinstance(explained, dict) else None
        entry_cursor = u16(entry_state, 0x49C)
        original_cursor = u16(original_state, 0x49C)
        modern_cursor = u16(modern_state, 0x49C)
        delta = (original_cursor - entry_cursor) & 0xFFFF
        if not isinstance(cursor, dict) or (
                cursor.get("state_offset"), cursor.get("released_code_offsets"),
                cursor.get("entry_value"), cursor.get("original_output_value"),
                cursor.get("original_observed_delta"),
                cursor.get("modern_replay_hundredth"),
                cursor.get("modern_output_value")) != (
                    "0x049c", ["0x0119", "0x4cae"], entry_cursor,
                    original_cursor, delta, 0, modern_cursor) or \
                delta != 0x57 or modern_cursor != entry_cursor:
            raise ValueError("RPG load-cursor clock explanation differs")

        persisted = explained.get("persisted_map_entity_base") \
            if isinstance(explained, dict) else None
        event_context = persisted.get("released_event_context") \
            if isinstance(persisted, dict) else None
        if not isinstance(event_context, dict):
            raise ValueError("persisted MAPZ entity-base explanation is missing")
        selector = (
            persisted.get("location_directory_offset"),
            event_context.get("field"), event_context.get("entity_index"))
        entity_offset, area, count = mapz_field_offset(entry_mapz, *selector)
        expected_base = area + 6
        base_hex = f"0x{expected_base:04x}"
        if (
                persisted.get("state_offset"),
                persisted.get("released_install_code_offset"),
                persisted.get("released_install_instruction"),
                persisted.get("area_image_offset"), persisted.get("expected_value"),
                persisted.get("entry_value"), persisted.get("modern_output_value"),
                persisted.get("original_output_value"),
                persisted.get("former_stale_modern_value"),
                persisted.get("released_opcode3_code_offset"),
                event_context.get("entity_count"),
                event_context.get("event_directory_offset"),
                event_context.get("before"), event_context.get("after"),
                event_context.get("mapz_file_offset")) != (
                    "0x0429", "0x119f", "89 36 29 04",
                    f"0x{area:04x}", base_hex, base_hex, base_hex, base_hex,
                    "0x42e5", "0x53f7", count, 224,
                    u16(entry_mapz, entity_offset), u16(modern_mapz, entity_offset),
                    f"0x{entity_offset:04x}") or \
                expected_base != 0x4491 or entity_offset != 0x46DB or \
                u16(entry_state, 0x429) != expected_base or \
                u16(modern_state, 0x429) != expected_base or \
                u16(original_state, 0x429) != expected_base or \
                u16(entry_mapz, entity_offset) != 0 or \
                u16(modern_mapz, entity_offset) != 3 or \
                u16(original_mapz, entity_offset) != 3:
            raise ValueError("persisted MAPZ entity-base mapping differs")

        chna = (args.game / "CHNA1.EXE").read_bytes()
        event = event_stream(chna, event_context["event_directory_offset"])
        if sha256(chna) != event_context.get("event_archive_sha256") or \
                sha256(event) != event_context.get("event_stream_sha256") or \
                not event.endswith(bytes.fromhex(
                    "03 00 03 00 03 00 1c 00 74 00 ff ff")):
            raise ValueError("released CHNA1 event 224 semantics differ")

        capture = reference.get("capture")
        if not isinstance(capture, dict) or \
                capture.get("game_path_layout") != "E:\\SWD2" or \
                capture.get("program") != "RPGMT.COM" or \
                capture.get("reference_program") != "RPG.EXE" or \
                capture.get("entry_method") != \
                "untouched title Continue from released save slot 1" or \
                capture.get("save_slot") != 1:
            raise ValueError("original RPG capture metadata differs")
        if (capture.get("capture_harness_sha256"),
                capture.get("capture_manifest_sha256")) != (
                    "626132a2380464aad548b300384004f51d91ba963cf16689e0638d3709cfb6c7",
                    "99e63d42aa4eeb7f7d7cee0222a5f6c7db2372e78b05917f80d23681b3244628"):
            raise ValueError("original title-loaded capture identity differs")
        if (capture.get("staged_save_sha256"),
                capture.get("staged_mapz_sha256"),
                capture.get("staged_name_sha256")) != (
                    sha256(entry_state), sha256(entry_mapz), sha256(entry_name)):
            raise ValueError("original title-load staging digest differs")
        autotype = args.reference.with_name(capture.get("autotype", ""))
        if sha256(autotype.read_bytes()) != capture.get("autotype_sha256"):
            raise ValueError("original RPG AUTOTYPE source differs")
        rpg_exe = (args.game / "RPG.EXE").read_bytes()
        if sha256(rpg_exe) != capture.get("reference_program_sha256"):
            raise ValueError("released RPG.EXE differs")
        if len(rpg_exe) < 0x20 or rpg_exe[:2] != b"MZ":
            raise ValueError("released RPG.EXE has no MZ header")
        image_start = u16(rpg_exe, 8) * 16
        load_cursor_clock = bytes.fromhex("b4 2c cd 21 b6 00 01 16 9c 04")
        if rpg_exe[image_start + 0x4CAE:
                   image_start + 0x4CAE + len(load_cursor_clock)] != \
                load_cursor_clock:
            raise ValueError("released RPG 4cae clock cursor path differs")
        install_instruction = bytes.fromhex("89 36 29 04")
        if rpg_exe[image_start + 0x119F:
                   image_start + 0x119F + len(install_instruction)] != \
                install_instruction:
            raise ValueError("released RPG SAVE+429 install path differs")
        opcode3_context = bytes.fromhex(
            "ad 8b 0e e6 3c d1 e1 f7 e1 03 06 29 04 "
            "03 06 da 3c 8b f8 ad 26 89 05")
        if rpg_exe[image_start + 0x53F7:
                   image_start + 0x53F7 + len(opcode3_context)] != \
                opcode3_context:
            raise ValueError("released RPG opcode-3 entity-base path differs")
        if (capture.get("wait_seconds"), capture.get("pace_seconds"),
                capture.get("time_limit_seconds")) != (7.5, 0.05, 95):
            raise ValueError("original RPG capture parameters differ")
        segments = capture.get("video_segments")
        if not isinstance(segments, list) or len(segments) != 2 or \
                [(item.get("bytes"), item.get("nb_frames"), item.get("sha256"))
                 for item in segments] != [
                    (44836, "1", "3dd16d6f996d92c65b2bea422853ddceb7bf14068ed37f312fa0ac9e8f892ec9"),
                    (16893684, "4700", "e70bc99ad329a09d3d04a0d190e45dc122a601383f432ba38c56a15f92b9bce1")]:
            raise ValueError("original RPG video segment metadata differs")
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        print(
            f"original mainline RPG boundary audit: FAIL: {error}",
            file=sys.stderr)
        return 1

    state_diff_count = len(reference["declared_state_byte_differences"])
    print(
        "original mainline RPG boundary audit: PARTIAL "
        f"(RPG {index} title-loaded MT->IF battle=74; MAPZ/NAME exact, "
        f"SAVE+429=4491h; declared state differences={state_diff_count}, "
        f"world original={original['world']} modern={modern['world']}; "
        "not byte-exact and not a full-playthrough verification)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
