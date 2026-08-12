#!/usr/bin/env python3
"""Replay and lock SWRO7's released behavior-five decoration cycle."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


HARNESS_SHA256 = "b884ff334ce081992422aa9a6b7e6a31685443c10869fa819d0fcb2a4e92b04b"
CROP = [160, 32, 96, 112]
RELEASE_FIELDS = [3328, 0, 1510, 5, 10, 65535, 65529, 4, 10, 58, 0]
FIXTURE_BEHAVIORS = [5, 3, 3]
TARGET_STATES = [
    {"world": [47, 11], "direction": 0, "animation": 1,
     "rewrite_frame": 1, "original_review_frame": 248},
    {"world": [47, 11], "direction": 0, "animation": 2,
     "rewrite_frame": 12, "original_review_frame": 329},
    {"world": [47, 11], "direction": 0, "animation": 3,
     "rewrite_frame": 23, "original_review_frame": 413},
    {"world": [47, 11], "direction": 0, "animation": 0,
     "rewrite_frame": 34, "original_review_frame": 498},
]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed SWRO7 decoration digest {label}")


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("MAPZ decoration oracle is out of bounds")
    return int.from_bytes(data[offset:offset + 2], "little")


def mapz_oracle(mapz: bytes) -> tuple[list[int], list[int]]:
    header = u16(mapz, 8) * 16
    image = mapz[header:]
    location = u16(image, 46)
    area = u16(image, location + 26)
    count = u16(image, area + 4)
    if header != 512 or location != 1500 or area != 19233 or count != 3:
        raise ValueError("SWRO7 decoration MAPZ structure differs")
    field = lambda number, index: u16(
        image, area + 6 + (number * count + index) * 2)
    return ([field(number, 0) for number in range(11)],
            [field(3, index) for index in range(count)])


def crop(data: bytes, bytes_per_pixel: int) -> bytes:
    x, y, width, height = CROP
    stride = 320 * bytes_per_pixel
    row_bytes = width * bytes_per_pixel
    x_bytes = x * bytes_per_pixel
    return b"".join(
        data[row * stride + x_bytes:row * stride + x_bytes + row_bytes]
        for row in range(y, y + height))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        pages = expected.get("matched_frames")
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_rpg_swro7_behavior5_decoration_sequence" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("map_location_directory_offset") != 46 or \
                expected.get("entity_index") != 0 or \
                expected.get("release_entity_fields") != RELEASE_FIELDS or \
                expected.get("fixture_behaviors") != FIXTURE_BEHAVIORS or \
                expected.get("player_world_position") != [30, 22] or \
                expected.get("initial_entity_world_position") != [47, 11] or \
                expected.get("field_direction_inputs") != 0 or \
                expected.get("confirm_inputs") != 0 or \
                expected.get("idle_polls") != 45 or \
                expected.get("observed_target_states") != TARGET_STATES or \
                not isinstance(pages, list) or len(pages) != 4 or \
                [page.get("rewrite_frame") for page in pages] != \
                    [state["rewrite_frame"] for state in TARGET_STATES] or \
                [page.get("original_review_frame") for page in pages] != \
                    [state["original_review_frame"] for state in TARGET_STATES] or \
                any(page.get("crop") != CROP for page in pages):
            raise ValueError("unsupported RPG SWRO7 decoration reference")

        release_mapz = (args.game / "MAPZ.DA1").read_bytes()
        fixture_mapz = (args.save_root / "MAPZ.DA1").read_bytes()
        if sha256((args.game / "RPG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("RPG.EXE differs from decoration reference")
        if sha256(release_mapz) != expected["release_mapz_sha256"] or \
                mapz_oracle(release_mapz)[0] != RELEASE_FIELDS:
            raise ValueError("release SWRO7 decoration animation differs")
        fixture_target, fixture_behaviors = mapz_oracle(fixture_mapz)
        if sha256(fixture_mapz) != expected["fixture_mapz_sha256"] or \
                fixture_target != RELEASE_FIELDS or \
                fixture_behaviors != FIXTURE_BEHAVIORS:
            raise ValueError("staged SWRO7 decoration animation differs")
        if tuple(sha256((args.game / "T1" / name).read_bytes()) for name in
                 ("SWRO7.RAP", "SWRO7.RSK", "SWRO7.RRO")) != (
                    expected["area_layout_sha256"],
                    expected["area_graphics_metadata_sha256"],
                    expected["area_overlay_sha256"]):
            raise ValueError("SWRO7 resources differ from decoration reference")
        if sha256((args.game / "SA" / "SA013.RSK").read_bytes()) != \
                expected["entity_sprite_archive_sha256"] or \
                sha256((args.game / "RX" / "TO01.RIX").read_bytes()) != \
                    expected["music_sha256"]:
            raise ValueError("SWRO7 sprite/music differs from decoration reference")

        save_digest = expected["fixture_save_sha256"]
        mapz_digest = expected["fixture_mapz_sha256"]
        name_digest = expected["name_sha256"]
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != save_digest or \
                sha256((args.save_root / "SAVE.DAQ").read_bytes()) != save_digest or \
                sha256((args.save_root / "MAPZ.DAQ").read_bytes()) != mapz_digest or \
                sha256((args.save_root / "NAME1.DSK").read_bytes()) != name_digest or \
                sha256((args.save_root / "NAMEQ.DSK").read_bytes()) != name_digest:
            raise ValueError("RPG SWRO7 decoration save triplets differ")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("RPG SWRO7 decoration input evidence differs")
        if expected.get("capture_wait_seconds") != 0 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 12 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_video_frames") != 840 or \
                expected.get("capture_review_frames") != 839 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != HARNESS_SHA256:
            raise ValueError("RPG SWRO7 decoration capture boundary differs")
        for name in ("release_mapz_sha256", "fixture_mapz_sha256",
                     "name_sha256", "area_layout_sha256",
                     "area_graphics_metadata_sha256", "area_overlay_sha256",
                     "entity_sprite_archive_sha256", "music_sha256",
                     "capture_harness_sha256", "capture_video_sha256",
                     "capture_manifest_sha256"):
            digest(expected.get(name), name)

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "OC", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"]:
            raise ValueError("RPG SWRO7 decoration replay timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("RPG SWRO7 decoration final state differs")
        if trace.get("transitions") != [{
                "module": "RPG.EXE", "input": "OC", "output": "--",
                "launched": True}]:
            raise ValueError("RPG SWRO7 decoration replay missed OC entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 46:
            raise ValueError("RPG SWRO7 decoration frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            compared_pixels = crop(pixels, 1)
            compared_rgb = crop(expand_rgb(pixels, palette), 3)
            if sha256(compared_pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(compared_rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(compared_rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "RPG SWRO7 decoration page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        print(
            "RPG SWRO7 decoration animation: 45 idle polls, released "
            "behavior-five frames 1/2/3/0 and complete 96x112 crops "
            "match the untouched original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG SWRO7 decoration animation: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
