#!/usr/bin/env python3
"""Replay and lock SBOUT's original automatic collision event."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


HARNESS_SHA256 = "b884ff334ce081992422aa9a6b7e6a31685443c10869fa819d0fcb2a4e92b04b"
CROP = [16, 115, 288, 80]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise ValueError(f"malformed SBOUT automatic-event digest {label}")


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("MAPZ automatic-event oracle is out of bounds")
    return int.from_bytes(data[offset:offset + 2], "little")


def entity_oracle(mapz: bytes) -> tuple[int, int, int, int]:
    header = u16(mapz, 8) * 16
    image = mapz[header:]
    location = u16(image, 12)
    area = u16(image, location + 26)
    count = u16(image, area + 4)
    if count != 10:
        raise ValueError("SBOUT entity count differs")
    field = lambda number, index: u16(
        image, area + 6 + (number * count + index) * 2)
    return field(2, 5), field(3, 5), field(8, 5), field(9, 5)


def crop(data: bytes, rectangle: list[int], bytes_per_pixel: int) -> bytes:
    x, y, width, height = rectangle
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
        kinds = ("initial_sbout_world_after_idle_poll",) + tuple(
            f"automatic_dialogue_direct_update_{index}" for index in range(9)) + \
            ("automatic_dialogue_final_page",)
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != \
                    "original_rpg_sbout_automatic_collision_event_sequence" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("map_location_directory_offset") != 12 or \
                expected.get("entity_index") != 5 or \
                expected.get("entity_cell_offset") != 25008 or \
                expected.get("entity_behavior") != 0 or \
                expected.get("entity_flags") != 0x800A or \
                expected.get("entity_event_directory_offset") != 220 or \
                expected.get("initial_world_position") != [79, 69] or \
                expected.get("requested_direction") != "RIGHT" or \
                expected.get("confirm_inputs") != 0 or \
                expected.get("final_world_position") != [79, 69] or \
                expected.get("capture_direction_pulses") != 8 or \
                not isinstance(pages, list) or len(pages) != 11 or \
                tuple(page.get("kind") for page in pages) != kinds or \
                tuple(page.get("rewrite_frame") for page in pages) != \
                    (1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) or \
                tuple(page.get("original_review_frame") for page in pages) != \
                    (351, 356, 359, 362, 366, 370, 374, 378, 382, 386, 389) or \
                pages[0].get("crop") is not None or \
                any(page.get("crop") != CROP for page in pages[1:]):
            raise ValueError("unsupported RPG SBOUT automatic-event reference")

        if sha256((args.game / "RPG.EXE").read_bytes()) != \
                expected["reference_program_sha256"]:
            raise ValueError("RPG.EXE differs from automatic-event reference")
        if tuple(sha256((args.game / "T1" / name).read_bytes()) for name in
                 ("SBOUT.RAP", "SBOUT.RSK", "SBOUT.RRO")) != (
                    expected["area_layout_sha256"],
                    expected["area_graphics_metadata_sha256"],
                    expected["area_overlay_sha256"]):
            raise ValueError("SBOUT resources differ from automatic-event reference")
        if sha256((args.game / "CHNA1.EXE").read_bytes()) != \
                expected["event_archive_sha256"] or \
                sha256((args.game / "CHNA1.DSK").read_bytes()) != \
                    expected["event_font_sha256"]:
            raise ValueError("CHNA1 resources differ from automatic-event reference")

        save_digest = expected["fixture_save_sha256"]
        mapz_digest = expected["mapz_sha256"]
        name_digest = expected["name_sha256"]
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != save_digest or \
                sha256((args.save_root / "SAVE.DAQ").read_bytes()) != save_digest or \
                sha256((args.save_root / "MAPZ.DA1").read_bytes()) != mapz_digest or \
                sha256((args.save_root / "MAPZ.DAQ").read_bytes()) != mapz_digest or \
                sha256((args.save_root / "NAME1.DSK").read_bytes()) != name_digest or \
                sha256((args.save_root / "NAMEQ.DSK").read_bytes()) != name_digest:
            raise ValueError("RPG SBOUT automatic-event save triplets differ")
        if entity_oracle((args.save_root / "MAPZ.DA1").read_bytes()) != \
                (25008, 0, 0x800A, 220):
            raise ValueError("SBOUT entity-five automatic-event oracle differs")

        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("RPG SBOUT automatic-event input evidence differs")
        if expected.get("capture_wait_seconds") != 4.7 or \
                expected.get("capture_pace_seconds") != 0.23 or \
                expected.get("capture_time_limit_seconds") != 11 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_video_frames") != 770 or \
                expected.get("capture_review_frames") != 769 or \
                expected.get("capture_dosbox_exit_code") != 0 or \
                expected.get("capture_harness_sha256") != HARNESS_SHA256:
            raise ValueError("RPG SBOUT automatic-event capture boundary differs")
        for name in ("mapz_sha256", "name_sha256", "area_layout_sha256",
                     "area_graphics_metadata_sha256", "area_overlay_sha256",
                     "event_archive_sha256", "event_font_sha256",
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
            raise ValueError("RPG SBOUT automatic-event replay timeline differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("RPG SBOUT automatic-event final state differs")
        if trace.get("transitions") != [{
                "module": "RPG.EXE", "input": "OC", "output": "--",
                "launched": True}]:
            raise ValueError("RPG SBOUT automatic-event replay missed OC entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 13:
            raise ValueError("RPG SBOUT automatic-event frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            rectangle = page.get("crop")
            compared_pixels = (pixels if rectangle is None else
                               crop(pixels, rectangle, 1))
            compared_rgb = (rgb if rectangle is None else
                            crop(rgb, rectangle, 3))
            if sha256(compared_pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(compared_rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(compared_rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    "RPG SBOUT automatic-event page differs from original: " +
                    page["kind"])
            digest(page.get("original_png_sha256"),
                   page["kind"] + "/original_png_sha256")
        print(
            "RPG SBOUT automatic collision event: no Confirm input, blocked "
            "world (79,69), one full page and ten 288x80 dialogue stages "
            "match the untouched original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"RPG SBOUT automatic collision event: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
