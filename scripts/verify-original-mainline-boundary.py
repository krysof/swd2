#!/usr/bin/env python3
"""Match a modern exported boundary to the locked untouched FIG Q state."""

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
CAPTURE_IDENTITIES = {
    4: {
        "capture_harness_sha256":
            "5cd8f7e75242b096454192b61c1287cb5dfe1600617f7fe63294c8516963750f",
        "capture_manifest_sha256":
            "45517de9ea08d57cc395df0ceeb0273266e293e5bf40ebabb40802ebc0667f82",
        "capture_video_sha256":
            "ed4c38b1b73a38bc341cd703c1a8215032e351a742eb41fcc4d592a3612e023b",
        "capture_video_frames": 4204,
        "capture_video_bytes": 13882300,
        "capture_harness_define": "FIG_STEP=1,FIG_FIXED_HUNDREDTH=0",
        "capture_harness_resident_paragraphs": 40,
        "random_encounter_entry_offset": 0,
        "random_encounter_fixed_hundredth": 0,
        "random_encounter_directory_offset": 116,
    },
    6: {
        "capture_harness_sha256":
            "b5ef8838d97cfe81882d4498559c07e4496f0e7c71443bc99b1a9cfe9c49ac0f",
        "capture_manifest_sha256":
            "939a195f450764413b19ef4fd4c33a3737ef8028f95134a0bd74da019afd0d3f",
        "capture_video_sha256":
            "1d1924112eb0392e8a97bb216091a32dd8fba4f76bb499faa69aa763a95979d3",
        "capture_video_frames": 4204,
        "capture_video_bytes": 13979408,
    },
    8: {
        "capture_harness_sha256":
            "f6947a955683d44e880a2fe25b828ee60637a0777515faa984beb60dc5e189a9",
        "capture_manifest_sha256":
            "c92b5f404c7383dbcfe095374a8a238383b2981ce11847a975a0549e06948a5c",
        "capture_video_prefix_sha256":
            "54f9da189c613c48368016251ae86244f13cb55afca4edd456a4816fe6815b21",
        "capture_video_prefix_frames": 73,
        "capture_video_prefix_bytes": 250622,
        "capture_video_sha256":
            "21cafc692ac904e13553431f96bf67adbd3db6e7de12b8b755b20127f27f2868",
        "capture_video_frames": 4131,
        "capture_video_bytes": 13572154,
        "capture_video_total_frames": 4204,
        "capture_video_total_bytes": 13822776,
        "capture_harness_define": "FIG_STEP=1,FIG_PHASE_HUNDREDTH=2",
        "random_encounter_entry_offset": 0,
        "random_encounter_directory_offset": 132,
    },
    10: {
        "capture_harness_sha256":
            "5cd8f7e75242b096454192b61c1287cb5dfe1600617f7fe63294c8516963750f",
        "capture_manifest_sha256":
            "67e14494dd323f070b54899ebba03df94ef156ccc78726343dad6a58703f9d0b",
        "capture_video_sha256":
            "ed3cd91f1d2b038348a7d66abcf1645c8bfbf737ebb5b99aca129a381e5ee463",
        "capture_video_frames": 4204,
        "capture_video_bytes": 13781388,
        "capture_harness_define": "FIG_STEP=1,FIG_FIXED_HUNDREDTH=0",
        "capture_harness_resident_paragraphs": 40,
        "random_encounter_entry_offset": 0,
        "random_encounter_fixed_hundredth": 0,
        "random_encounter_directory_offset": 132,
    },
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fnv1a64(data: bytes) -> str:
    digest = FNV_OFFSET
    for value in data:
        digest ^= value
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def artifact(root: Path, value: object) -> bytes:
    if not isinstance(value, dict) or not isinstance(value.get("path"), str):
        raise ValueError("boundary artifact is malformed")
    path = root / value["path"]
    data = path.read_bytes()
    if value.get("bytes") != len(data) or value.get("fnv1a64") != fnv1a64(data):
        raise ValueError(f"boundary artifact differs: {value.get('path')}")
    return data


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
                    1, "original_mainline_module_boundary_state_checkpoint",
                    "exact_state_checkpoint"):
            raise ValueError("original boundary reference schema differs")
        limitation = reference.get("limitation")
        if not isinstance(limitation, str) or \
                "STEP.OUT is not the evidence source" not in limitation or \
                "not a complete original playthrough trace" not in limitation:
            raise ValueError("original boundary limitation is missing")
        if manifest.get("status") != "complete":
            raise ValueError("modern module boundary capture is incomplete")
        index = reference.get("modern_invocation_index")
        invocations = manifest.get("invocations")
        if not isinstance(index, int) or not isinstance(invocations, list) or \
                index >= len(invocations):
            raise ValueError("modern invocation index is invalid")
        invocation = invocations[index]
        expected_header = (
            reference.get("modern_module"), reference.get("input_marker"),
            reference.get("output_marker"), reference.get("input_range"))
        actual_header = (
            invocation.get("module"), invocation.get("input_marker"),
            invocation.get("output_marker"), invocation.get("input_range"))
        if actual_header != expected_header:
            raise ValueError("modern module invocation identity/range differs")
        begin = invocation["input_range"]["begin"]
        end = invocation["input_range"]["end"]
        if manifest.get("input_steps", [])[begin:end] != reference.get("input_steps"):
            raise ValueError("modern module input slice differs")

        entry = invocation.get("entry")
        leave = invocation.get("exit")
        if not isinstance(entry, dict) or not isinstance(leave, dict):
            raise ValueError("modern boundary snapshots are missing")
        entry_state = artifact(args.boundaries, entry.get("state"))
        entry_transfer = artifact(args.boundaries, entry.get("transfer"))
        output_state = artifact(args.boundaries, leave.get("state"))
        output_transfer = artifact(args.boundaries, leave.get("transfer"))
        output_mapz = artifact(args.boundaries, leave.get("mapz"))
        output_name = artifact(args.boundaries, leave.get("name"))
        if (fnv1a64(entry_state) != reference.get("entry_state_fnv1a64") or
                sha256(entry_state) != reference.get("entry_state_sha256") or
                sha256(entry_transfer) != reference.get("entry_transfer_sha256") or
                fnv1a64(output_state) != reference.get("output_state_fnv1a64") or
                sha256(output_state) != reference.get("output_state_sha256") or
                sha256(output_transfer) != reference.get("output_transfer_sha256") or
                fnv1a64(output_mapz) != reference.get("mapz_fnv1a64") or
                sha256(output_mapz) != reference.get("mapz_sha256") or
                fnv1a64(output_name) != reference.get("name_fnv1a64") or
                sha256(output_name) != reference.get("name_sha256")):
            raise ValueError("modern/original boundary state digest differs")
        if entry_transfer != b"IF" + entry_state or \
                output_transfer != b"OC" + output_state:
            raise ValueError("modern SharedTransfer bytes differ from DOS ABI")

        # This is the compatibility fault exposed by the first chained RPG
        # resume: MAPZ strings omit a drive, but RPG:10fd persists one.  A
        # drive-less modern boundary left untouched RPG.EXE on a blank page.
        # Both sides of the proven FIG handoff must retain released E: paths.
        for state in (entry_state, output_state):
            for offset, size in PATH_FIELDS:
                path = state[offset:offset + size].split(b"\0", 1)[0]
                if path and not path.startswith(b"E:"):
                    raise ValueError(
                        f"persisted resource path lacks E: at {offset:#x}: {path!r}")

        capture = reference.get("capture")
        if not isinstance(capture, dict):
            raise ValueError("original capture metadata is missing")
        autotype = args.reference.with_name(capture.get("autotype", ""))
        if sha256(autotype.read_bytes()) != capture.get("autotype_sha256"):
            raise ValueError("original boundary AUTOTYPE source differs")
        if sha256((args.game / "FIG.EXE").read_bytes()) != \
                capture.get("reference_program_sha256"):
            raise ValueError("released FIG.EXE differs")
        for name in ("capture_harness_sha256", "capture_manifest_sha256",
                     "capture_video_sha256"):
            value = capture.get(name)
            if not isinstance(value, str) or len(value) != 64:
                raise ValueError(f"original {name} is malformed")
        if (
                capture.get("state_evidence_file"),
                capture.get("state_evidence_sha256"),
                capture.get("state_evidence_bytes"),
                capture.get("transfer_reconstruction"),
                capture.get("staged_entry_state_sha256"),
                capture.get("staged_entry_transfer_sha256")) != (
                    "SAVE.DAQ", sha256(output_state), len(output_state),
                    "OC marker plus released SAVE.DAQ", sha256(entry_state),
                    sha256(entry_transfer)):
            raise ValueError("original FIG SAVE.DAQ evidence summary differs")
        capture_identity = CAPTURE_IDENTITIES.get(index)
        if capture_identity is None or any(
                capture.get(name) != value
                for name, value in capture_identity.items()):
            raise ValueError("original FIG capture identity differs")
        if (capture.get("wait_seconds"), capture.get("pace_seconds"),
                capture.get("time_limit_seconds")) != (14.5, 1.0, 60):
            raise ValueError("original boundary capture parameters differ")
        random_entry = capture.get("random_encounter_entry_offset")
        if random_entry is not None and (
                int.from_bytes(entry_state[0x4A0:0x4A2], "little") !=
                random_entry):
            raise ValueError("original FIG random-encounter entry differs")
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        print(f"original mainline boundary validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "original mainline boundary validation: OK "
        f"(FIG {index} IF->OC state={reference['output_state_fnv1a64']}, "
        "released SAVE.DAQ and reconstructed OC transfer exact, "
        "MAPZ/NAME exact, E: RPG resume paths retained, "
        "capture identity locked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
