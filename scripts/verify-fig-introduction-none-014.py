#!/usr/bin/env python3
"""Replay and prove FIG's generic introduction without a prompt."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from swd2_frame_capture import expand_rgb, load_indexed_frames


DATA_SEGMENT_PARAGRAPHS = 0x0E03
FINAL_WAIT_BYPASS = 0x2A40


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def valid_digest(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        char in "0123456789abcdef" for char in value
    )


def load_image(executable: bytes) -> bytes:
    if executable[:2] != b"MZ" or len(executable) < 0x1C:
        raise ValueError("FIG.EXE is not an MZ executable")
    header = int.from_bytes(executable[8:10], "little") * 16
    if header <= 0 or header >= len(executable):
        raise ValueError("FIG.EXE has an invalid MZ header")
    return executable[header:]


def require_bytes(image: bytes, offset: int, expected_hex: str,
                  label: str) -> None:
    expected = bytes.fromhex(expected_hex)
    if image[offset:offset + len(expected)] != expected:
        raise ValueError(f"FIG {label} machine code differs")


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
        pages = expected.get("frames")
        rewrite_frames = [0, 1, 3, 4, 5, 6]
        if expected.get("schema_version") != 1 or \
                expected.get("kind") != "original_fig_introduction_without_prompt_014" or \
                expected.get("formation_directory_offset") != 0x014 or \
                not isinstance(pages, list) or len(pages) != 6 or \
                [page.get("rewrite_frame") for page in pages] != rewrite_frames:
            raise ValueError("unsupported FIG no-prompt introduction reference")

        executable = (args.game / "FIG.EXE").read_bytes()
        if sha256(executable) != expected["reference_program_sha256"]:
            raise ValueError("FIG.EXE differs from the introduction reference")
        image = load_image(executable)
        # 3ed3's nominal DATA:2a40 bypass is dead in the shipped image: the
        # byte starts at zero, and its only two literal references are the
        # comparison followed by an explicit clear. Therefore YN/NY records
        # also run the animated final-$$ acknowledgement before 5c98.
        require_bytes(image, 0x3ED3, "803e402a01743e", "final-text wait branch")
        require_bytes(
            image, 0x3F20, "c606452a00c606402a00c3",
            "final-text flag reset")
        flag_offset = DATA_SEGMENT_PARAGRAPHS * 16 + FINAL_WAIT_BYPASS
        if flag_offset >= len(image) or image[flag_offset] != 0:
            raise ValueError("FIG DATA:2a40 does not start cleared")
        references = [
            index for index in range(len(image) - 1)
            if image[index:index + 2] == b"\x40\x2a"
        ]
        if references != [0x3ED5, 0x3F27]:
            raise ValueError(
                "FIG DATA:2a40 reference domain differs: " +
                repr([hex(value) for value in references]))

        autotype = args.reference.with_name(expected["capture_autotype"])
        if sha256(autotype.read_bytes()) != expected["capture_autotype_sha256"]:
            raise ValueError("original introduction capture input differs")
        if sha256((args.save_root / "SAVE.DA1").read_bytes()) != \
                expected["fixture_save_sha256"]:
            raise ValueError("FIG 014h introduction staged save differs")

        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.output / "trace.json"
        frame_path = args.output / "frames.bin"
        subprocess.run([
            str(args.executable), "--game", str(args.game),
            "--save-dir", str(args.save_root), "--slot", "1", "--no-save",
            "--start-marker", "IF", "--run-replay",
            str(args.reference.with_name("replay-fig-introduction-none.txt")),
            "--trace-output", str(trace_path),
            "--frame-output", str(frame_path),
        ], check=True, stdout=subprocess.DEVNULL)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != {
                "total": 6, "consumed": 6, "remaining": 0,
                "implicit_quit_calls": 0} or trace.get("boundaries") != {
                    "wait": 1, "poll": 4, "text": 1, "frontend": 0}:
            raise ValueError("FIG 014h introduction input boundaries differ")
        if trace.get("video") != {
                "frames": 8, "direct_updates": 2,
                "last_width": 320, "last_height": 200,
                "fnv1a64": expected["rewrite_video_fnv1a64"]}:
            raise ValueError("FIG 014h introduction video differs")
        if trace.get("audio") != {
                "music_calls": 1, "voice_calls": 0,
                "stop_music_calls": 0, "stop_audio_calls": 1,
                "fnv1a64": "971fb031ff7f6f85"} or \
                trace.get("delay_milliseconds") != 115:
            raise ValueError("FIG 014h introduction audio or timing differs")
        if (trace.get("state_fnv1a64"), trace.get("mapz_fnv1a64"),
                trace.get("name_fnv1a64")) != (
                    expected["rewrite_state_fnv1a64"],
                    "827f0f1b725a0958", "e3d2853e2676513b"):
            raise ValueError("FIG 014h introduction final state differs")
        if trace.get("transitions") != [{
                "module": "FIG.EXE", "input": "IF", "output": "--",
                "launched": True}]:
            raise ValueError("FIG 014h introduction return boundary differs")

        frames = load_indexed_frames(frame_path)
        if len(frames) != 8:
            raise ValueError("FIG 014h introduction frame count differs")
        for page in pages:
            pixels, palette = frames[page["rewrite_frame"]]
            rgb = expand_rgb(pixels, palette)
            if sha256(pixels) != page["rewrite_indexed_sha256"] or \
                    sha256(palette) != page["rewrite_palette_sha256"] or \
                    sha256(rgb) != page["rewrite_rgb_sha256"] or \
                    sha256(rgb) != page["original_rgb_sha256"]:
                raise ValueError(
                    f"FIG 014h introduction {page['kind']} no longer matches original")
            if not valid_digest(page.get("original_png_sha256")):
                raise ValueError("malformed introduction original PNG digest")
        for name in (
                "capture_harness_sha256", "capture_video_sha256",
                "capture_manifest_sha256"):
            if not valid_digest(expected.get(name)):
                raise ValueError(f"malformed introduction evidence digest {name}")
        print(
            "FIG 014h no-prompt introduction: bare enemy page, first glyph, "
            "all four animated cursor frames, and six exact original RGB "
            "pages; the unobserved command transition is not claimed")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FIG 014h no-prompt introduction: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
