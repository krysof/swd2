#!/usr/bin/env python3
"""Capture an isolated original SWD2 run through DOSBox-X.

This produces an RGB/video reference for reverse-engineering.  It deliberately
does not claim SWD2FRM2 indexed-palette parity: DOSBox-X's video capture has
already converted VGA indices through the DAC palette.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


TOKEN = re.compile(r"^[A-Za-z0-9_.,-]+$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_autotype(path: Path | None) -> list[str]:
    if path is None:
        return []
    text = "\n".join(
        line.split("#", 1)[0] for line in path.read_text(encoding="utf-8").splitlines()
    )
    tokens = text.split()
    for token in tokens:
        if not TOKEN.fullmatch(token):
            raise ValueError(f"invalid AUTOTYPE token: {token!r}")
    return tokens


def command_version(command: str) -> str:
    result = subprocess.run(
        # DOSBox-X prints a valid version banner but deliberately exits 1 for
        # this informational option, so the banner rather than the status is
        # the compatibility boundary here.
        [command, "-version"], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not lines or not lines[0].startswith("DOSBox-X version "):
        raise RuntimeError(f"cannot determine DOSBox-X version from {command}")
    return lines[0]


def video_metadata(ffprobe: str, path: Path) -> dict[str, object]:
    result = subprocess.run(
        [
            ffprobe, "-v", "error", "-select_streams", "v:0",
            "-show_entries",
            "stream=codec_name,width,height,r_frame_rate,avg_frame_rate,nb_frames,duration",
            "-of", "json", str(path),
        ],
        check=True, text=True, stdout=subprocess.PIPE,
    )
    streams = json.loads(result.stdout).get("streams", [])
    if len(streams) != 1:
        raise RuntimeError("DOSBox capture does not contain exactly one video stream")
    stream = streams[0]
    width = int(stream.get("width", 0))
    height = int(stream.get("height", 0))
    if width <= 0 or height <= 0:
        raise RuntimeError("DOSBox capture has invalid video dimensions")
    try:
        duration = float(stream.get("duration", 0))
        frame_count = int(stream.get("nb_frames", 0))
    except (TypeError, ValueError) as error:
        raise RuntimeError("DOSBox capture has invalid duration/frame metadata") from error
    if duration <= 0 or frame_count <= 0:
        raise RuntimeError(
            "DOSBox capture is empty or incomplete "
            f"(duration={duration}, frames={frame_count})"
        )
    return stream


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--game", type=Path, default=root / "game")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--autotype", type=Path)
    parser.add_argument("--program", default="SWD2.EXE")
    parser.add_argument(
        "--reference-program",
        help=(
            "original EXE whose behavior is being observed when --program is "
            "a small launch harness"
        ),
    )
    parser.add_argument("--wait", type=float, default=2.0)
    parser.add_argument("--pace", type=float, default=1.0)
    parser.add_argument("--time-limit", type=int, default=60)
    parser.add_argument(
        "--extract-fps", type=float, default=0.0,
        help="also extract nearest-neighbour 320x200 RGB PNG review frames",
    )
    args = parser.parse_args()

    try:
        dosbox = shutil.which("dosbox-x")
        ffprobe = shutil.which("ffprobe")
        ffmpeg = shutil.which("ffmpeg")
        if not dosbox:
            raise RuntimeError("dosbox-x is required")
        if not ffprobe:
            raise RuntimeError("ffprobe is required")
        if args.extract_fps > 0 and not ffmpeg:
            raise RuntimeError("ffmpeg is required when --extract-fps is used")
        if not args.game.is_dir():
            raise RuntimeError(f"game directory does not exist: {args.game}")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+\.(?:EXE|COM)", args.program,
                            re.IGNORECASE):
            raise ValueError(
                "--program must be a plain DOS .EXE or .COM filename")
        program = args.game / args.program
        if not program.is_file():
            raise RuntimeError(f"original program does not exist: {program}")
        reference_name = args.reference_program or args.program
        if not re.fullmatch(r"[A-Za-z0-9_.-]+\.(?:EXE|COM)", reference_name,
                            re.IGNORECASE):
            raise ValueError(
                "--reference-program must be a plain DOS .EXE or .COM filename")
        reference_program = args.game / reference_name
        if not reference_program.is_file():
            raise RuntimeError(
                f"reference program does not exist: {reference_program}")
        if args.wait < 0 or args.wait > 30:
            raise ValueError("--wait must be between 0 and 30 seconds")
        if args.pace < 0.01 or args.pace > 10:
            raise ValueError("--pace must be between 0.01 and 10 seconds")
        if args.time_limit < 1 or args.time_limit > 86_400:
            raise ValueError("--time-limit must be between 1 and 86400 seconds")
        if args.extract_fps < 0 or args.extract_fps > 1000:
            raise ValueError("--extract-fps must be between 0 and 1000")

        dosbox_version = command_version(dosbox)

        tokens = read_autotype(args.autotype)
        args.output.mkdir(parents=True, exist_ok=True)
        manifest_path = args.output / "manifest.json"
        video_path = args.output / "original.avi"
        log_path = args.output / "dosbox.log"
        for path in (manifest_path, video_path, log_path):
            if path.exists():
                raise RuntimeError(f"refusing to overwrite existing capture artifact: {path}")

        with tempfile.TemporaryDirectory(prefix="swd2-dosbox-") as temporary:
            temporary_path = Path(temporary)
            dos_root = temporary_path / "drive-c"
            dos_game = dos_root / "SWD2"
            captures = temporary_path / "captures"
            shutil.copytree(args.game, dos_game)
            captures.mkdir()

            command = [
                dosbox, "-silent", "-fastlaunch", "-exit", "-defaultconf",
                "-defaultmapper", "-time-limit", str(args.time_limit),
                "-set", "sdl output=surface",
                "-set", f"dosbox captures={captures}",
                "-set", "mixer nosound=true",
                "-c", f"mount c {dos_root}",
                "-c", "c:",
                "-c", "cd swd2",
                # Freeze DOS calendar/time so MEO's challenge cursor and any
                # time-dependent RPG branch can be reproduced on another host.
                "-c", "date 08-02-1994",
                "-c", "time 12:00",
            ]
            if tokens:
                command += [
                    "-c",
                    "autotype -w " + format(args.wait, "g") +
                    " -p " + format(args.pace, "g") + " " + " ".join(tokens),
                ]
            command += [
                "-c", f"dx-capture /v /-a /-d {args.program}",
                "-c", "exit",
            ]
            result = subprocess.run(
                command, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            log_path.write_text(result.stdout, encoding="utf-8")
            if result.returncode != 0:
                raise RuntimeError(
                    f"DOSBox-X exited with status {result.returncode}; "
                    f"see {log_path}"
                )
            videos = sorted(captures.glob("*.avi"))
            if len(videos) != 1:
                raise RuntimeError(
                    f"DOSBox-X produced {len(videos)} AVI files instead of one; "
                    f"see {log_path}"
                )
            shutil.move(videos[0], video_path)

        metadata = video_metadata(ffprobe, video_path)
        frame_artifacts: list[dict[str, object]] = []
        if args.extract_fps > 0:
            frames = args.output / "frames"
            if frames.exists():
                raise RuntimeError(f"refusing to reuse frame directory: {frames}")
            frames.mkdir()
            subprocess.run(
                [
                    ffmpeg, "-v", "error", "-i", str(video_path),
                    "-vf",
                    f"fps={format(args.extract_fps, 'g')},"
                    "scale=320:200:flags=neighbor",
                    str(frames / "%06d.png"),
                ],
                check=True,
            )
            for frame in sorted(frames.glob("*.png")):
                frame_artifacts.append({
                    "path": str(frame.relative_to(args.output)),
                    "bytes": frame.stat().st_size,
                    "sha256": sha256(frame),
                })
            if not frame_artifacts:
                raise RuntimeError("DOSBox review-frame extraction produced no frames")

        autotype_bytes = (args.autotype.read_bytes()
                          if args.autotype is not None else b"")
        manifest = {
            "schema_version": 1,
            "kind": "dosbox_rgb_reference_capture",
            "status": "reference_only",
            "limitation": (
                "AVI/PNG frames are RGB observations after the emulated VGA DAC; "
                "they are not SWD2FRM2 indexed-palette completion evidence"
            ),
            "dosbox": dosbox_version,
            "program": args.program,
            "program_sha256": sha256(program),
            # A harness can reproduce an original overlay-entry contract while
            # leaving the reference executable byte-for-byte untouched.  Keep
            # both identities so a harness hash can never be mistaken for the
            # executable whose behavior the capture demonstrates.
            "reference_program": reference_name,
            "reference_program_sha256": sha256(reference_program),
            "game_path_layout": "C:\\SWD2",
            "dos_date": "1994-08-02",
            "dos_time": "12:00:00",
            "autotype": {
                "source_sha256": hashlib.sha256(autotype_bytes).hexdigest(),
                "wait_seconds": args.wait,
                "pace_seconds": args.pace,
                "tokens": tokens,
            },
            "time_limit_seconds": args.time_limit,
            "video": {
                "path": video_path.name,
                "bytes": video_path.stat().st_size,
                "sha256": sha256(video_path),
                **metadata,
            },
            "review_extract_fps": args.extract_fps,
            "review_frames": frame_artifacts,
            "dosbox_exit_code": result.returncode,
        }
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(
            f"DOSBox-X original reference: {video_path} "
            f"({metadata.get('nb_frames', '?')} frames)"
        )
        print(f"Reference manifest: {manifest_path}")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"original reference capture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
