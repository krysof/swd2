#!/usr/bin/env python3
"""Replay and lock FIG's captured-ally player-buff presentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


REFERENCE_PROGRAM_SHA256 = (
    "c4e0f0d04d25b70f80ab3d3490a81ef78c375193ccac70dad5b09c186c758876"
)
CAPTURE_HARNESS_SHA256 = (
    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
            char not in "0123456789abcdef" for char in value):
        raise ValueError(f"malformed captured-ally buff digest {label}")
    return value


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


def archive_record(image: bytes, index: int) -> bytes:
    directory_bytes = word(image, 2)
    sentinel = word(image, 0)
    offsets = [word(image, offset)
               for offset in range(0, directory_bytes, 2)]
    if directory_bytes < 4 or directory_bytes % 2 or index >= len(offsets):
        raise ValueError("ITEM.EXE directory is malformed")
    start = offsets[index]
    finish = min((value for value in offsets if value > start),
                 default=sentinel)
    if start > finish or finish > len(image):
        raise ValueError("ITEM.EXE record bounds are malformed")
    return image[start:finish]


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
                    "original_fig_captured_ally_player_buff" or \
                expected.get("status") != "exact_rgb_checkpoint" or \
                expected.get("formation_directory_offset") != 392 or \
                expected.get("captured_item_id") != 390 or \
                expected.get("captured_ally_ability_id") != 38 or \
                expected.get("captured_ally_ability_slot") != "special_a" or \
                expected.get("effect_code") != 0x67 or \
                expected.get("target_flags") != 0x8300 or \
                expected.get("random_cursor") != 0x1002 or \
                not isinstance(pages, list) or len(pages) != 55 or \
                [page.get("rewrite_frame") for page in pages] != \
                    list(range(1, 56)) or \
                pages[30].get("kind") != \
                    "captured ally attack-buff status card":
            raise ValueError("unsupported FIG captured-ally buff reference")

        fig_path = args.game / "FIG.EXE"
        if sha256(fig_path.read_bytes()) != REFERENCE_PROGRAM_SHA256 or \
                expected.get("reference_program_sha256") != \
                    REFERENCE_PROGRAM_SHA256 or \
                expected.get("capture_harness_sha256") != \
                    CAPTURE_HARNESS_SHA256:
            raise ValueError("FIG captured-ally buff program boundary differs")
        fig = mz_image(fig_path)
        table = word(fig, 1) * 16 + 0x1DCC
        ability = fig[table + 38 * 20:table + 39 * 20]
        item = archive_record(mz_image(args.game / "ITEM.EXE"), 390 + 2)
        if len(ability) != 20 or word(ability, 12) != 0x8300 or \
                word(ability, 14) != 0x67 or len(item) < 0x50 or \
                (item[5] & 2) == 0 or word(item, 0x42) != 38:
            raise ValueError("shipped captured-ally buff descriptor differs")

        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG captured-ally buff staged save differs")
        autotype = args.reference.with_name(expected["capture_autotype"])
        replay = args.reference.with_name(expected["replay_input"])
        if sha256(autotype.read_bytes()) != \
                expected["capture_autotype_sha256"] or \
                sha256(replay.read_bytes()) != expected["replay_input_sha256"]:
            raise ValueError("FIG captured-ally buff input evidence differs")
        if expected.get("capture_wait_seconds") != 5 or \
                expected.get("capture_pace_seconds") != 0.5 or \
                expected.get("capture_time_limit_seconds") != 25 or \
                expected.get("capture_review_fps") != 70 or \
                expected.get("capture_review_frames") != 1749 or \
                expected.get("capture_video_frames") != 1751 or \
                expected.get("capture_dosbox_exit_code") != 0:
            raise ValueError("FIG captured-ally buff capture boundary differs")
        for name in ("capture_video_sha256", "capture_manifest_sha256"):
            digest(expected.get(name), name)

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay", str(replay),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != expected["rewrite_input"] or \
                trace.get("boundaries") != expected["rewrite_boundaries"] or \
                trace.get("video") != expected["rewrite_video"] or \
                trace.get("audio") != expected["rewrite_audio"] or \
                trace.get("delay_milliseconds") != \
                    expected["rewrite_delay_milliseconds"] or \
                trace.get("frame_fnv1a64", [])[-1:] != [
                    expected["rewrite_final_fnv1a64"]] or \
                (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                 trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    expected["rewrite_mapz_fnv1a64"],
                    expected["rewrite_name_fnv1a64"]):
            raise ValueError("FIG captured-ally buff deterministic trace differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG captured-ally buff missed IF entry")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 56:
            raise ValueError("FIG captured-ally buff frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG captured-ally buff {page['kind']} differs")
            digest(page.get("original_png_sha256"),
                   f"{page['kind']}/original_png_sha256")
            if not isinstance(page.get("original_review_frame"), int):
                raise ValueError("captured-ally buff review frame differs")

        print(
            "FIG captured-ally player-buff checkpoint: 1048 leaves the "
            "target player's ordinary pose-zero card under effect 67h; all "
            "55 stable submitted pages exactly match original 320x200 RGB")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG captured-ally player-buff checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
