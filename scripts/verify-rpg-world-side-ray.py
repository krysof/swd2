#!/usr/bin/env python3
"""Replay and lock BUIN1 lateral-ray interaction."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


HARNESS_SHA256 = "b884ff334ce081992422aa9a6b7e6a31685443c10869fa819d0fcb2a4e92b04b"
RELEASE_FIELDS = [5120, 6, 60878, 1, 10, 0, 65520, 4, 10, 40, 1]
FIXTURE_BEHAVIORS = [1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed BUIN1 side-ray digest {label}")


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("MAPZ interaction oracle is out of bounds")
    return int.from_bytes(data[offset:offset + 2], "little")


def mapz_oracle(mapz: bytes) -> tuple[list[int], list[int]]:
    header = u16(mapz, 8) * 16
    image = mapz[header:]
    location = u16(image, 268)
    area = u16(image, location + 26)
    count = u16(image, area + 4)
    if header != 512 or location != 4608 or area != 23549 or count != 11:
        raise ValueError("BUIN1 side-ray MAPZ structure differs")
    field = lambda number, index: u16(
        image, area + 6 + (number * count + index) * 2)
    return ([field(number, 0) for number in range(11)],
            [field(3, index) for index in range(count)])


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
                    "original_rpg_buin1_side_ray_interaction_sequence" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("map_location_directory_offset") != 268 or \
                expected.get("entity_index") != 0 or \
                expected.get("release_entity_fields") != RELEASE_FIELDS or \
                expected.get("fixture_behaviors") != FIXTURE_BEHAVIORS or \
                expected.get("player_world_position") != [18, 168] or \
                expected.get("player_direction") != 6 or \
                expected.get("entity_world_footprint") != \
                    [[15, 169], [16, 169], [17, 169]] or \
                expected.get("entity_initial_direction") != 6 or \
                expected.get("entity_faced_direction") != 9 or \
                expected.get("event_directory_offset") != 40 or \
                expected.get("automatic_event_flag") is not False or \
                expected.get("empty_center_ray") != \
                    [[17, 168], [16, 168], [15, 168], [14, 168]] or \
                expected.get("selected_side_ray") != 1 or \
                expected.get("selected_side_ray_first_cell") != [17, 169] or \
                not isinstance(pages, list) or len(pages) != 4 or \
                [page.get("rewrite_frame") for page in pages] != [0, 2, 3, 4] or \
                [page.get("original_review_frame") for page in pages] != \
                    [238, 463, 501, 505]:
            raise ValueError("unsupported RPG BUIN1 side-ray reference")

        release_mapz = (args.game / "MAPZ.DA1").read_bytes()
        fixture_mapz = (args.save_root / "MAPZ.DA1").read_bytes()
        if sha256((args.game / "RPG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("RPG.EXE differs from interaction reference")
        if sha256(release_mapz) != expected["release_mapz_sha256"] or \
                mapz_oracle(release_mapz)[0] != RELEASE_FIELDS:
            raise ValueError("release BUIN1 side-ray entity differs")
        fixture_target, fixture_behaviors = mapz_oracle(fixture_mapz)
        if sha256(fixture_mapz) != expected["fixture_mapz_sha256"] or \
                fixture_target != RELEASE_FIELDS or \
                fixture_behaviors != FIXTURE_BEHAVIORS:
            raise ValueError("staged BUIN1 side-ray entity differs")
        if tuple(sha256((args.game / "T4" / name).read_bytes()) for name in
                 ("BUIN1.RAP", "BUIN1.RSK", "BUIN1.RRO")) != (
                    expected["area_layout_sha256"],
                    expected["area_graphics_metadata_sha256"],
                    expected["area_overlay_sha256"]):
            raise ValueError("BUIN1 resources differ from interaction reference")
        if sha256((args.game / "SA" / "SA020.RSK").read_bytes()) != \
                expected["entity_sprite_archive_sha256"] or \
                sha256((args.game / "RX" / "RI011.RIX").read_bytes()) != \
                    expected["music_sha256"] or \
                sha256((args.game / "CHNA2.EXE").read_bytes()) != \
                    expected["event_archive_sha256"] or \
                sha256((args.game / "CHNA2.DSK").read_bytes()) != \
                    expected["event_font_sha256"]:
            raise ValueError("BUIN1 sprite/event/audio evidence differs")

        save_digest = expected["fixture_save_sha256"]
        mapz_digest = expected["fixture_mapz_sha256"]
        name_digest = expected["name_sha256"]
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != save_digest or \
                sha256((args.save_root / "SAVE.DAQ").read_bytes()) != save_digest or \
                sha256((args.save_root / "MAPZ.DAQ").read_bytes()) != mapz_digest or \
                sha256((args.save_root / "NAME1.DSK").read_bytes()) != name_digest or \
                sha256((args.save_root / "NAMEQ.DSK").read_bytes()) != name_digest:
            raise ValueError("RPG BUIN1 side-ray save triplets differ")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("RPG BUIN1 side-ray input evidence differs")
        if expected.get("capture_wait_seconds") != 6.5 or \
                expected.get("capture_pace_seconds") != 1 or \
                expected.get("capture_time_limit_seconds") != 16 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_video_frames") != 1121 or \
                expected.get("capture_review_frames") != 1120 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != HARNESS_SHA256 or \
                expected.get("capture_limitation") != (
                    "The final scheduled close key was not observed as a "
                    "restored world page in this RGB run; restoration is "
                    "independently covered by the centre-ray BUIN1 reference. "
                    "The four pages here prove selection of the lateral ray "
                    "and event entry."):
            raise ValueError("RPG BUIN1 side-ray capture boundary differs")
        for name in (
                "release_mapz_sha256", "fixture_mapz_sha256", "name_sha256",
                "area_layout_sha256", "area_graphics_metadata_sha256",
                "area_overlay_sha256", "entity_sprite_archive_sha256",
                "music_sha256", "event_archive_sha256", "event_font_sha256",
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
            raise ValueError("RPG BUIN1 side-ray replay timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("RPG BUIN1 side-ray final state differs")
        if trace.get("transitions") != [{
                "module": "RPG.EXE", "input": "OC", "output": "--",
                "launched": True}]:
            raise ValueError("RPG BUIN1 side-ray replay missed OC entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 6:
            raise ValueError("RPG BUIN1 side-ray frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "RPG BUIN1 side-ray page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        print(
            "RPG BUIN1 side-ray: empty centre ray, selected south lateral "
            "ray and entity event entry "
            "match the untouched original in four complete RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG BUIN1 side-ray: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
