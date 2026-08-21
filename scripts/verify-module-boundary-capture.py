#!/usr/bin/env python3
"""Validate a fail-closed module-boundary export and all referenced bytes."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path, PurePosixPath


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MARKERS = {
    "--": 0,
    "01": 1,
    "MT": ord("M") | (ord("T") << 8),
    "IF": ord("I") | (ord("F") << 8),
    "ED": ord("E") | (ord("D") << 8),
    "OC": ord("O") | (ord("C") << 8),
    "OM": ord("O") | (ord("M") << 8),
}
MODULES = {"MEO.EXE", "RPG.EXE", "FIG.EXE", "DEMO.EXE"}
BOUNDARIES = {"ANY", "WAIT", "POLL", "TEXT", "FRONTEND"}
ACTIONS = {
    "NONE", "UP", "DOWN", "LEFT", "RIGHT", "PAGEUP", "PAGEDOWN",
    "HOME", "END", "CONFIRM", "CANCEL", "QUIT", "ERASE",
}


def fnv1a64(data: bytes) -> str:
    digest = FNV_OFFSET
    for value in data:
        digest ^= value
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def safe_artifact(root: Path, value: object) -> tuple[Path, bytes]:
    if not isinstance(value, dict):
        raise ValueError("artifact entry is not an object")
    relative_text = value.get("path")
    if not isinstance(relative_text, str):
        raise ValueError("artifact path is missing")
    relative = PurePosixPath(relative_text)
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"unsafe artifact path: {relative_text!r}")
    path = root.joinpath(*relative.parts)
    if not path.is_file():
        raise ValueError(f"artifact does not exist: {relative_text}")
    data = path.read_bytes()
    if value.get("bytes") != len(data):
        raise ValueError(f"artifact size differs: {relative_text}")
    if value.get("fnv1a64") != fnv1a64(data):
        raise ValueError(f"artifact digest differs: {relative_text}")
    return path, data


def validate_snapshot(root: Path, snapshot: object, marker: str,
                      referenced: set[Path]) -> dict[str, object]:
    if not isinstance(snapshot, dict) or snapshot.get("marker") != marker:
        raise ValueError(f"snapshot marker differs from {marker}")
    for key in ("consumed_inputs", "frames", "timeline_events",
                "at_milliseconds"):
        if not isinstance(snapshot.get(key), int) or snapshot[key] < 0:
            raise ValueError(f"snapshot {key} is invalid")

    paths: dict[str, object] = {}
    blobs: dict[str, bytes | None] = {}
    for kind in ("transfer", "state", "name"):
        path, data = safe_artifact(root, snapshot.get(kind))
        referenced.add(path.resolve())
        paths[kind] = snapshot[kind]
        blobs[kind] = data
    if snapshot.get("mapz") is None:
        paths["mapz"] = None
        blobs["mapz"] = None
    else:
        path, data = safe_artifact(root, snapshot.get("mapz"))
        referenced.add(path.resolve())
        paths["mapz"] = snapshot["mapz"]
        blobs["mapz"] = data

    transfer = blobs["transfer"]
    state = blobs["state"]
    name = blobs["name"]
    assert transfer is not None and state is not None and name is not None
    if len(state) != 0x546 or len(transfer) != 0x548:
        raise ValueError("SAVE/SharedTransfer byte size differs from DOS ABI")
    marker_word = MARKERS.get(marker)
    if marker_word is None:
        raise ValueError(f"unknown marker in snapshot: {marker!r}")
    if transfer[:2] != marker_word.to_bytes(2, "little"):
        raise ValueError("SharedTransfer marker bytes differ")
    if transfer[2:] != state:
        raise ValueError("SharedTransfer payload differs from state artifact")
    if not name:
        raise ValueError("NAME artifact is empty")
    return {"metadata": snapshot, "artifacts": paths, "blobs": blobs}


def validate(args: argparse.Namespace) -> tuple[int, int, str, str | None]:
    root = args.capture
    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError("complete manifest.json is missing")
    if (root / "manifest.partial.json").exists():
        raise ValueError("partial manifest remains beside complete evidence")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or manifest.get("status") != "complete":
        raise ValueError("boundary manifest schema/status differs")
    inputs = manifest.get("input")
    if (not isinstance(inputs, dict) or inputs.get("remaining") != 0 or
            inputs.get("consumed") != inputs.get("total")):
        raise ValueError("boundary capture did not consume every strict input")
    input_steps = manifest.get("input_steps")
    if not isinstance(input_steps, list) or len(input_steps) != inputs.get("total"):
        raise ValueError("boundary capture input step list differs")
    for index, step in enumerate(input_steps):
        if not isinstance(step, str) or step.count(":") != 1:
            raise ValueError(f"input step {index} is malformed")
        boundary, action = step.split(":")
        if boundary not in BOUNDARIES or action not in ACTIONS:
            raise ValueError(f"input step {index} is unknown: {step!r}")
        if boundary == "FRONTEND" and action not in {"NONE", "QUIT"}:
            raise ValueError(f"frontend input step {index} is invalid")
    invocations = manifest.get("invocations")
    if not isinstance(invocations, list):
        raise ValueError("boundary invocations are missing")
    if (manifest.get("invocation_count") != len(invocations) or
            manifest.get("transition_count") != len(invocations)):
        raise ValueError("boundary/transition counts differ")
    if args.expected_invocations is not None and len(invocations) != args.expected_invocations:
        raise ValueError(
            f"captured {len(invocations)} invocations, expected {args.expected_invocations}")

    referenced: set[Path] = {manifest_path.resolve()}
    prior_exit: dict[str, object] | None = None
    figure_count = 0
    last_exit: dict[str, object] | None = None
    for index, invocation in enumerate(invocations):
        if not isinstance(invocation, dict) or invocation.get("index") != index:
            raise ValueError(f"invocation index {index} is invalid")
        module = invocation.get("module")
        input_marker = invocation.get("input_marker")
        output_marker = invocation.get("output_marker")
        if module not in MODULES:
            raise ValueError(f"invocation {index} has unknown module")
        if input_marker not in MARKERS or output_marker not in MARKERS:
            raise ValueError(f"invocation {index} has unknown marker")
        if module == "FIG.EXE":
            figure_count += 1

        entry = validate_snapshot(
            root, invocation.get("entry"), input_marker, referenced)
        exit_snapshot = validate_snapshot(
            root, invocation.get("exit"), output_marker, referenced)
        entry_metadata = entry["metadata"]
        exit_metadata = exit_snapshot["metadata"]
        assert isinstance(entry_metadata, dict) and isinstance(exit_metadata, dict)
        for range_name, metadata_name in (
            ("input_range", "consumed_inputs"),
            ("frame_range", "frames"),
            ("timeline_range", "timeline_events"),
            ("millisecond_range", "at_milliseconds"),
        ):
            range_value = invocation.get(range_name)
            if (not isinstance(range_value, dict) or
                    range_value.get("begin") != entry_metadata[metadata_name] or
                    range_value.get("end") != exit_metadata[metadata_name] or
                    range_value["end"] < range_value["begin"]):
                raise ValueError(f"invocation {index} {range_name} differs")

        if prior_exit is not None:
            prior_metadata = prior_exit["metadata"]
            assert isinstance(prior_metadata, dict)
            for key in ("consumed_inputs", "frames", "timeline_events",
                        "at_milliseconds"):
                if entry_metadata[key] != prior_metadata[key]:
                    raise ValueError(
                        f"invocation {index} is discontinuous at {key}")
            prior_artifacts = prior_exit["artifacts"]
            entry_artifacts = entry["artifacts"]
            assert isinstance(prior_artifacts, dict) and isinstance(entry_artifacts, dict)
            for key in ("state", "mapz", "name"):
                if entry_artifacts[key] != prior_artifacts[key]:
                    raise ValueError(
                        f"invocation {index} mutates {key} between modules")
        prior_exit = exit_snapshot
        last_exit = exit_snapshot

    if args.expected_figures is not None and figure_count != args.expected_figures:
        raise ValueError(
            f"captured {figure_count} FIG invocations, expected {args.expected_figures}")
    if last_exit is None:
        raise ValueError("boundary capture contains no invocation")
    last_metadata = last_exit["metadata"]
    assert isinstance(last_metadata, dict)
    if (invocations[0]["entry"]["consumed_inputs"] != 0 or
            last_metadata["consumed_inputs"] != len(input_steps)):
        raise ValueError("module ranges do not cover the complete input stream")
    last_artifacts = last_exit["artifacts"]
    assert isinstance(last_artifacts, dict)
    final_state = last_artifacts["state"]
    final_mapz = last_artifacts["mapz"]
    assert isinstance(final_state, dict)
    if (args.expected_final_state is not None and
            final_state.get("fnv1a64") != args.expected_final_state):
        raise ValueError("final boundary state digest differs")
    if args.expected_final_mapz is not None:
        if not isinstance(final_mapz, dict) or final_mapz.get("fnv1a64") != args.expected_final_mapz:
            raise ValueError("final boundary MAPZ digest differs")

    actual_files = {
        path.resolve() for path in root.rglob("*") if path.is_file()
    }
    if actual_files != referenced:
        extras = sorted(str(path.relative_to(root.resolve()))
                        for path in actual_files - referenced)
        missing = sorted(str(path.relative_to(root.resolve()))
                         for path in referenced - actual_files)
        raise ValueError(
            f"unreferenced/missing boundary files: extras={extras}, missing={missing}")
    return len(invocations), figure_count, final_state["fnv1a64"], (
        final_mapz.get("fnv1a64") if isinstance(final_mapz, dict) else None)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--expected-invocations", type=int)
    parser.add_argument("--expected-figures", type=int)
    parser.add_argument("--expected-final-state")
    parser.add_argument("--expected-final-mapz")
    args = parser.parse_args()
    try:
        invocations, figures, state, mapz = validate(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"module boundary capture validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "module boundary capture validation: OK "
        f"({invocations} invocations, {figures} FIG, state={state}, mapz={mapz})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
