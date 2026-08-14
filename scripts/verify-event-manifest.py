#!/usr/bin/env python3
"""Verify the exhaustive, deterministic CHNA event-slot manifest."""

from __future__ import annotations

import hashlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


EXPECTED_SHA256 = "3fbbf8b58a3f26470f90d2a6e842999351ec9ac1ca2bd1b6b6f95baef3213ac3"
EXPECTED_SUMMARY = {
    "archives": 7,
    "slots": 1768,
    "physical_records": 1607,
    "commands": 7462,
    "reachable_slots": 1065,
    "unreachable_slots": 703,
    "reachable_physical_records": 1064,
    "unreachable_physical_records": 543,
    "directory_fnv1a64": "ca749c59020d6a1f",
    "unreachable_directory_fnv1a64": "4562b736f681dbee",
}
EXPECTED_ARCHIVE_SLOTS = {
    "CHNA0.EXE": 441,
    "CHNA1.EXE": 221,
    "CHNA2.EXE": 230,
    "CHNA3.EXE": 255,
    "CHNA4.EXE": 91,
    "CHNA5.EXE": 288,
    "CHNA6.EXE": 242,
}
EXPECTED_OPCODES = set(range(62)) - {10, 11}
HEX64 = re.compile(r"[0-9a-f]{16}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify-event-manifest.py MANIFEST.json")
    path = Path(sys.argv[1])
    try:
        payload = path.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        if digest != EXPECTED_SHA256:
            raise ValueError(
                f"manifest SHA-256 is {digest}, expected {EXPECTED_SHA256}")
        document = json.loads(payload)
        if document.get("schema") != "swd2-chna-event-manifest-v1":
            raise ValueError("event manifest schema differs")
        if document.get("summary") != EXPECTED_SUMMARY:
            raise ValueError("event manifest summary differs")
        slots = document.get("slots")
        if not isinstance(slots, list) or len(slots) != EXPECTED_SUMMARY["slots"]:
            raise ValueError("event slot count differs")

        by_archive: dict[str, list[dict[str, object]]] = defaultdict(list)
        opcode_union: set[int] = set()
        physical: set[tuple[str, int]] = set()
        reachable_physical: set[tuple[str, int]] = set()
        unreachable_physical: set[tuple[str, int]] = set()
        command_count = 0
        reachable_count = 0
        previous_key: tuple[str, int] | None = None
        for index, slot in enumerate(slots):
            if not isinstance(slot, dict):
                raise ValueError(f"slot {index} is not an object")
            archive = slot.get("archive")
            target = slot.get("target")
            physical_offset = slot.get("physical_offset")
            reachable = slot.get("reachable")
            byte_length = slot.get("byte_length")
            commands = slot.get("command_count")
            opcodes = slot.get("opcodes")
            stream_digest = slot.get("stream_fnv1a64")
            if archive not in EXPECTED_ARCHIVE_SLOTS:
                raise ValueError(f"slot {index} has unknown archive {archive!r}")
            if not isinstance(target, int) or target < 20 or target & 1:
                raise ValueError(f"slot {index} has invalid target {target!r}")
            key = (archive, target)
            if previous_key is not None and key <= previous_key:
                raise ValueError(f"slot directory is not strictly sorted at {key!r}")
            previous_key = key
            if not isinstance(physical_offset, int) or physical_offset < 20 or physical_offset & 1:
                raise ValueError(f"slot {key!r} has invalid physical offset")
            if not isinstance(reachable, bool):
                raise ValueError(f"slot {key!r} has invalid reachability")
            if not isinstance(byte_length, int) or byte_length <= 0:
                raise ValueError(f"slot {key!r} has invalid byte length")
            if not isinstance(commands, int) or commands <= 0:
                raise ValueError(f"slot {key!r} has invalid command count")
            if not isinstance(opcodes, list) or opcodes != sorted(set(opcodes)) or \
                    any(not isinstance(opcode, int) or opcode not in range(62)
                        for opcode in opcodes):
                raise ValueError(f"slot {key!r} has invalid opcode set")
            if not isinstance(stream_digest, str) or not HEX64.fullmatch(stream_digest):
                raise ValueError(f"slot {key!r} has invalid stream digest")

            by_archive[archive].append(slot)
            opcode_union.update(opcodes)
            command_count += commands
            reachable_count += int(reachable)
            physical_key = (archive, physical_offset)
            physical.add(physical_key)
            (reachable_physical if reachable else unreachable_physical).add(physical_key)

        if set(by_archive) != set(EXPECTED_ARCHIVE_SLOTS):
            raise ValueError("archive set differs")
        for archive, expected_count in EXPECTED_ARCHIVE_SLOTS.items():
            archive_slots = by_archive[archive]
            if len(archive_slots) != expected_count:
                raise ValueError(f"{archive} slot count differs")
            expected_targets = list(range(20, 20 + 2 * expected_count, 2))
            if [slot["target"] for slot in archive_slots] != expected_targets:
                raise ValueError(f"{archive} event directory is not exhaustive")
        if opcode_union != EXPECTED_OPCODES:
            raise ValueError(f"shipped opcode union differs: {sorted(opcode_union)!r}")
        if command_count != EXPECTED_SUMMARY["commands"]:
            raise ValueError("decoded command count differs")
        if reachable_count != EXPECTED_SUMMARY["reachable_slots"]:
            raise ValueError("reachable slot count differs")
        if len(physical) != EXPECTED_SUMMARY["physical_records"]:
            raise ValueError("physical record count differs")
        # Directory aliases are classified per slot above.  A physical record
        # is dead only when none of its aliases is reachable; CHNA0 deliberately
        # has one unused alias for the otherwise reachable record at 902.
        unreachable_physical -= reachable_physical
        if len(reachable_physical) != EXPECTED_SUMMARY["reachable_physical_records"] or \
                len(unreachable_physical) != EXPECTED_SUMMARY["unreachable_physical_records"]:
            raise ValueError("physical record reachability counts differ")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"CHNA event manifest validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "CHNA event manifest validation: OK "
        "(1768/1768 slots, including 703 unreachable slots)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
