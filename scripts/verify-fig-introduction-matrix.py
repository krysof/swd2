#!/usr/bin/env python3
"""Prove exact stable RGB pages across all 51 FIG introduction records."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


DIRECTORY_BYTES = 0x4E4
DATA_SEGMENT_PARAGRAPHS = 0x0E03
FINAL_WAIT_BYPASS = 0x2A40
CAPTURE_HARNESS_SHA256 = \
    "f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba"
SPECIALIZED = {
    "fig-introduction-none-014-rgb-reference.json": 0x014,
    "fig-introduction-multipage-rgb-reference.json": 0x03A,
    "fig-introduction-two-page-rgb-reference.json": 0x042,
    "fig-introduction-none-rgb-reference.json": 0x094,
    "fig-introduction-yn-rgb-reference.json": 0x0B2,
    "fig-introduction-ny-1e4-rgb-reference.json": 0x1E4,
    "fig-introduction-yn-1e8-rgb-reference.json": 0x1E8,
    "fig-introduction-none-2ce-rgb-reference.json": 0x2CE,
    "fig-introduction-none-2d8-rgb-reference.json": 0x2D8,
    "fig-introduction-2e8-rgb-reference.json": 0x2E8,
    "fig-introduction-yn-34c-rgb-reference.json": 0x34C,
    "fig-introduction-ny-rgb-reference.json": 0x386,
    "fig-introduction-3e6-rgb-reference.json": 0x3E6,
    "fig-introduction-none-3e8-rgb-reference.json": 0x3E8,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def valid_digest(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        char in "0123456789abcdef" for char in value)


def word(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("ORC word lies outside the data image")
    return int.from_bytes(data[offset:offset + 2], "little")


def mz_image(executable: bytes, label: str) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1C:
        raise ValueError(f"{label} is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError(f"{label} has an invalid MZ header")
    return executable[header:]


def introduction_directory(orc_executable: bytes) -> dict[int, str]:
    """Return the first directory byte offset for every introduction record."""
    image = mz_image(orc_executable, "ORC.EXE")
    if len(image) < DIRECTORY_BYTES:
        raise ValueError("ORC directory is truncated")
    directory = [word(image, offset) for offset in range(0, DIRECTORY_BYTES, 2)]
    first_growth = directory[0]
    starts = sorted({
        value for value in directory
        if DIRECTORY_BYTES <= value < first_growth
    })
    parsed: dict[int, str] = {}
    for index, start in enumerate(starts):
        finish = starts[index + 1] if index + 1 < len(starts) else first_growth
        cursor = start + 20 + 8
        count = word(image, cursor)
        cursor += 2
        if count == 0 or count > 5:
            raise ValueError("ORC encounter combatant count differs")
        cursor += count * 4
        if word(image, cursor) == 0x2323:
            cursor += 4
        text_words: list[int] = []
        while cursor + 2 <= finish and word(image, cursor) != 0x2121:
            text_words.append(word(image, cursor))
            cursor += 2
        if cursor + 2 > finish:
            raise ValueError("ORC encounter introduction terminator differs")
        prompt = "--"
        if len(text_words) >= 2 and text_words[-1] in (0x4E59, 0x594E):
            prompt = "YN" if text_words[-1] == 0x4E59 else "NY"
            text_words.pop()
        if text_words and text_words[-1] != 0x2424:
            raise ValueError("ORC encounter introduction page terminator differs")
        if 0x2424 in text_words:
            parsed[start] = prompt
    output: dict[int, str] = {}
    for start, prompt in parsed.items():
        # Several random-encounter directory entries alias the same record.
        # BattleDatabase::encounters() owns it once and the original helper
        # reports the first directory slot, so close that same 51-record set.
        output[directory.index(start) * 2] = prompt
    return output


def require_fig_machine_code(executable: bytes) -> None:
    image = mz_image(executable, "FIG.EXE")
    if image[0x3ED3:0x3EDA] != bytes.fromhex("803e402a01743e"):
        raise ValueError("FIG final-text wait branch differs")
    if image[0x3F20:0x3F2B] != bytes.fromhex("c606452a00c606402a00c3"):
        raise ValueError("FIG final-text flag reset differs")
    flag = DATA_SEGMENT_PARAGRAPHS * 16 + FINAL_WAIT_BYPASS
    if flag >= len(image) or image[flag] != 0:
        raise ValueError("FIG DATA:2a40 does not start cleared")
    references = [
        offset for offset in range(len(image) - 1)
        if image[offset:offset + 2] == b"\x40\x2a"
    ]
    if references != [0x3ED5, 0x3F27]:
        raise ValueError("FIG DATA:2a40 reference domain differs")


def validate_specialized(script_dir: Path, names: object) -> set[int]:
    if not isinstance(names, list) or len(names) != len(SPECIALIZED) or \
            set(names) != set(SPECIALIZED):
        raise ValueError("specialized introduction reference list differs")
    offsets: set[int] = set()
    for name, offset in SPECIALIZED.items():
        data = json.loads((script_dir / name).read_text(encoding="utf-8"))
        if data.get("formation_directory_offset") != offset:
            raise ValueError(f"specialized introduction offset differs: {name}")
        pairs = [
            page for page in data.get("frames", [])
            if page.get("rewrite_rgb_sha256") == page.get("original_rgb_sha256")
            and valid_digest(page.get("rewrite_rgb_sha256"))
        ]
        if not pairs:
            raise ValueError(f"specialized introduction has no exact RGB page: {name}")
        offsets.add(offset)
    return offsets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.reference.read_text(encoding="utf-8"))
        records = expected.get("records")
        if expected.get("schema_version") != 1 or expected.get("kind") != \
                "original_fig_introduction_remaining_record_matrix" or \
                not isinstance(records, list) or len(records) != 37:
            raise ValueError("unsupported FIG introduction matrix reference")

        fig = (args.game / "FIG.EXE").read_bytes()
        if sha256(fig) != expected.get("reference_program_sha256"):
            raise ValueError("FIG.EXE differs from the introduction matrix")
        require_fig_machine_code(fig)
        script_dir = args.reference.parent
        specialized = validate_specialized(
            script_dir, expected.get("specialized_reference_files"))
        database = introduction_directory((args.game / "ORC.EXE").read_bytes())
        if len(database) != 51:
            raise ValueError(f"ORC introduction directory has {len(database)} records")

        matrix_offsets = {
            row.get("formation_directory_offset") for row in records
            if isinstance(row, dict)
        }
        if len(matrix_offsets) != 37 or None in matrix_offsets or \
                matrix_offsets & specialized or \
                matrix_offsets | specialized != set(database):
            raise ValueError("introduction matrix does not close all 51 ORC records")

        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        base_save = (args.game / "SAVE.DA1").read_bytes()
        total_pages = 0
        for row in records:
            offset = row["formation_directory_offset"]
            prompt = row["prompt_order"]
            if row.get("introduction_page_count") != 1 or \
                    database[offset] != prompt:
                raise ValueError(f"ORC {offset:03x}h introduction metadata differs")
            pages = row.get("frames")
            rewrite_indices = [page.get("rewrite_frame") for page in pages]
            if not isinstance(pages, list) or rewrite_indices[:6] != \
                    [0, 1, 3, 4, 5, 6] or len(set(rewrite_indices)) != len(pages):
                raise ValueError(f"ORC {offset:03x}h stable RGB coverage differs")
            observable = {
                "--": {0, 1, 3, 4, 5, 6, 7},
                "YN": {0, 1, 3, 4, 5, 6, 7, 8},
                # Original NY captures request RIGHT but not LEFT.  Modern
                # frame 9 is a replay-only return to the same default pixels,
                # so it is not double-counted as original sequence evidence.
                "NY": {0, 1, 3, 4, 5, 6, 7, 8},
            }[prompt]
            if not set(rewrite_indices) <= observable or \
                    row.get("unobserved_rewrite_frames") != sorted(
                        observable - set(rewrite_indices)):
                raise ValueError(f"ORC {offset:03x}h rewrite frame domain differs")

            autotype = script_dir / row["capture_autotype"]
            prompt_autotypes = {
                "--": {"original-fig-introduction-none-autotype.txt"},
                "YN": {"original-fig-introduction-yn-autotype.txt"},
                "NY": {"original-fig-introduction-ny-autotype.txt"},
            }[prompt]
            prompt_autotypes |= {
                "original-fig-introduction-late-confirm-autotype.txt",
                "original-fig-introduction-very-late-confirm-autotype.txt",
                "original-fig-introduction-confirm-sweep-autotype.txt",
                "original-fig-introduction-phase-confirm-autotype.txt",
                "original-fig-introduction-phase-select-autotype.txt",
                "original-fig-introduction-cycle-select-autotype.txt",
            }
            if autotype.name not in prompt_autotypes or \
                    row["capture_harness_sha256"] != CAPTURE_HARNESS_SHA256:
                raise ValueError(f"ORC {offset:03x}h capture contract differs")
            if sha256(autotype.read_bytes()) != row["capture_autotype_sha256"]:
                raise ValueError(f"ORC {offset:03x}h original input differs")
            for name in ("fixture_save_sha256", "capture_harness_sha256",
                         "capture_video_sha256", "capture_manifest_sha256"):
                if not valid_digest(row.get(name)):
                    raise ValueError(f"ORC {offset:03x}h malformed evidence digest {name}")

            work = args.output / f"{offset:03x}"
            save_root = work / "save"
            save_root.mkdir(parents=True)
            for name in ("MAPZ.DA1", "NAME1.DSK"):
                shutil.copy2(args.game / name, save_root / name)
            save = bytearray(base_save)
            save[0x4A0:0x4A2] = offset.to_bytes(2, "little")
            if sha256(save) != row["fixture_save_sha256"]:
                raise ValueError(f"ORC {offset:03x}h staged save differs")
            (save_root / "SAVE.DA1").write_bytes(save)

            replay = script_dir / {
                "--": "replay-fig-introduction-none.txt",
                "YN": "replay-fig-introduction-yn.txt",
                "NY": "replay-fig-introduction-ny.txt",
            }[prompt]
            trace_path = work / "trace.json"
            frame_path = work / "frames.bin"
            subprocess.run([
                str(args.executable), "--game", str(args.game),
                "--save-dir", str(save_root), "--slot", "1", "--no-save",
                "--start-marker", "IF", "--run-replay", str(replay),
                "--trace-output", str(trace_path),
                "--frame-output", str(frame_path),
            ], check=True, stdout=subprocess.DEVNULL)
            trace = json.loads(trace_path.read_text(encoding="utf-8"))
            waits = {"--": 1, "YN": 2, "NY": 3}[prompt]
            frame_count = {"--": 8, "YN": 9, "NY": 10}[prompt]
            if trace.get("input") != {
                    "total": waits + 5, "consumed": waits + 5,
                    "remaining": 0, "implicit_quit_calls": 0} or \
                    trace.get("boundaries") != {
                    "wait": waits, "poll": 4, "text": 1, "frontend": 0}:
                raise ValueError(f"ORC {offset:03x}h input boundaries differ")
            if trace.get("video") != {
                    "frames": frame_count, "direct_updates": 2,
                    "last_width": 320, "last_height": 200,
                    "fnv1a64": row["rewrite_video_fnv1a64"]}:
                raise ValueError(f"ORC {offset:03x}h video trace differs")
            if trace.get("audio") != {
                    "music_calls": 1, "voice_calls": 0,
                    "stop_music_calls": 0, "stop_audio_calls": 1,
                    "fnv1a64": "971fb031ff7f6f85"} or \
                    trace.get("delay_milliseconds") != 115:
                raise ValueError(f"ORC {offset:03x}h audio/timing differs")
            if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                    trace.get("name_fnv1a64")) != (
                    row["rewrite_state_fnv1a64"], "827f0f1b725a0958",
                    "e3d2853e2676513b"):
                raise ValueError(f"ORC {offset:03x}h final state differs")
            output_marker = "--" if prompt == "--" else "OC"
            if trace.get("transitions") != [{
                    "module": "FIG.EXE", "input": "IF",
                    "output": output_marker, "launched": True}]:
                raise ValueError(f"ORC {offset:03x}h module boundary differs")

            frames = load_indexed_frames(frame_path)
            if len(frames) != frame_count:
                raise ValueError(f"ORC {offset:03x}h capture length differs")
            for page in pages:
                if not isinstance(page.get("original_review_frame"), int) or \
                        page["original_review_frame"] <= 0:
                    raise ValueError(f"ORC {offset:03x}h review frame differs")
                pixels, palette = frames[page["rewrite_frame"]]
                rgb = expand_rgb(pixels, palette)
                if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                        sha256(palette) != page["rewrite_palette_sha256"] or \
                        sha256(rgb) != page["rewrite_rgb_sha256"] or \
                        sha256(rgb) != page["original_rgb_sha256"] or \
                        not valid_digest(page.get("original_png_sha256")):
                    raise ValueError(
                        f"ORC {offset:03x}h {page.get('kind')} differs from original")
            total_pages += len(pages)

        print(
            f"FIG introduction matrix: all 51 nonempty ORC records closed; "
            f"{len(records)} remaining records replayed with {total_pages} exact "
            "original RGB pages, including every stable default/alternate prompt "
            "and command page")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG introduction matrix: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
