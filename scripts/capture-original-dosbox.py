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
import time
from pathlib import Path


TOKEN = re.compile(r"^[A-Za-z0-9_.,-]+$")
FATAL_AUTOTYPE_LOG_MARKERS = (
    "MAPPER: Couldn't find a button named ",
    "AUTOTYPE: invalid",
    "AUTOTYPE: stopping",
)
FATAL_RUNTIME_LOG_MARKERS = FATAL_AUTOTYPE_LOG_MARKERS + (
    "ERROR CPU:Illegal Unhandled Interrupt Called 6",
)
MAX_LOG_BYTES = 16 * 1024 * 1024
RESERVED_OUTPUT_NAMES = {
    "dosbox.log",
    "frames",
    "manifest.json",
    "original.avi",
}


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


def validate_autotype_log(output: str) -> None:
    """Reject captures whose scheduled key stream did not finish cleanly."""
    for marker in FATAL_AUTOTYPE_LOG_MARKERS:
        if marker.lower() in output.lower():
            offending = next(
                (line.strip() for line in output.splitlines()
                 if marker.lower() in line.lower()), marker)
            raise RuntimeError(
                "DOSBox-X rejected part of the AUTOTYPE stream: " + offending)


def validate_runtime_log(output: str) -> None:
    """Reject a complete diagnostic log containing any fatal boundary."""
    if len(output.encode("utf-8", errors="replace")) > MAX_LOG_BYTES:
        raise RuntimeError(
            "DOSBox-X diagnostic output exceeded the fixed "
            f"{MAX_LOG_BYTES}-byte evidence limit"
        )
    validate_autotype_log(output)
    for marker in FATAL_RUNTIME_LOG_MARKERS[len(FATAL_AUTOTYPE_LOG_MARKERS):]:
        if marker.lower() in output.lower():
            offending = next(
                (line.strip() for line in output.splitlines()
                 if marker.lower() in line.lower()), marker)
            raise RuntimeError(
                "DOSBox-X reported a fatal runtime boundary: " + offending)


def validate_collect_files(names: list[str]) -> None:
    """Keep collected DOS outputs unique and separate from capture products."""
    seen: set[str] = set()
    for name in names:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name in {".", ".."}:
            raise ValueError("--collect-file must be a plain DOS filename")
        normalized = name.casefold()
        if normalized in seen:
            raise ValueError(
                "--collect-file names must be unique ignoring DOS case: " + name)
        if normalized in RESERVED_OUTPUT_NAMES or re.fullmatch(
                r"(?:original|failed)-[0-9]{3}\.avi", normalized):
            raise ValueError(
                "--collect-file conflicts with a capture artifact: " + name)
        seen.add(normalized)


def normalize_dos_drive(value: str) -> str:
    """Return a safe uppercase DOS drive letter for the isolated mount."""
    if re.fullmatch(r"[A-Za-z]", value) is None:
        raise ValueError("--dos-drive must be one ASCII drive letter")
    return value.upper()


def preserve_failed_videos(captures: Path, output: Path) -> list[Path]:
    """Keep interrupted video prefixes without promoting them to evidence."""
    preserved: list[Path] = []
    for index, source in enumerate(sorted(captures.glob("*.avi"))):
        destination = output / f"failed-{index:03d}.avi"
        if destination.exists():
            raise RuntimeError(
                "refusing to overwrite failed capture artifact: "
                f"{destination}"
            )
        shutil.copy2(source, destination)
        preserved.append(destination)
    return preserved


def run_dosbox(command: list[str], log_path: Path, timeout: float) -> tuple[int, str]:
    """Run DOSBox while bounding and actively checking its diagnostic log."""
    deadline = time.monotonic() + timeout
    process: subprocess.Popen[bytes] | None = None
    scan_offset = 0
    scan_tail = b""
    fatal_line = ""
    try:
        with log_path.open("wb") as log_stream:
            process = subprocess.Popen(
                command, stdout=log_stream, stderr=subprocess.STDOUT)
            while process.poll() is None:
                log_stream.flush()
                size = log_stream.tell()
                if size > MAX_LOG_BYTES:
                    fatal_line = (
                        "DOSBox-X diagnostic output exceeded the fixed "
                        f"{MAX_LOG_BYTES}-byte evidence limit"
                    )
                    break
                if size > scan_offset:
                    with log_path.open("rb") as reader:
                        reader.seek(scan_offset)
                        chunk = reader.read(size - scan_offset)
                    scan_offset = size
                    searchable = (scan_tail + chunk).decode(
                        "utf-8", errors="replace")
                    lowered = searchable.lower()
                    marker = next((item for item in FATAL_RUNTIME_LOG_MARKERS
                                   if item.lower() in lowered), None)
                    if marker is not None:
                        fatal_line = next(
                            (line.strip() for line in searchable.splitlines()
                             if marker.lower() in line.lower()), marker)
                        break
                    scan_tail = (scan_tail + chunk)[-512:]
                if time.monotonic() >= deadline:
                    fatal_line = "DOSBox-X exceeded the host-side capture timeout"
                    break
                time.sleep(0.1)

            if fatal_line:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            else:
                process.wait()
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()

    output = log_path.read_text(encoding="utf-8", errors="replace")
    if fatal_line:
        raise RuntimeError(f"{fatal_line}; see {log_path}")
    # The process can emit its last diagnostic and exit between two polling
    # iterations.  Re-scan the complete log so a final CPU/AUTOTYPE fault
    # cannot evade the live monitor and still receive a manifest.
    validate_runtime_log(output)
    return process.returncode, output


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
        "--dos-drive", default="C",
        help=(
            "isolated DOS mount letter (default C); use E when reproducing "
            "the released SWD2 path-persistence boundary"
        ),
    )
    parser.add_argument(
        "--temporary-root", type=Path,
        help=(
            "place the isolated DOS drive and in-progress AVI files under "
            "this directory instead of the host system temporary volume"
        ),
    )
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
    parser.add_argument(
        "--allow-video-segments", action="store_true",
        help=(
            "retain every DOSBox-X AVI when a released mode switch splits "
            "one program run into multiple capture segments"
        ),
    )
    parser.add_argument(
        "--collect-file", action="append", default=[],
        help=(
            "copy a plain DOS output filename from the isolated game "
            "directory into the capture output after a successful run; may "
            "be repeated"
        ),
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
        if args.temporary_root is not None and not args.temporary_root.is_dir():
            raise RuntimeError(
                f"temporary capture root does not exist: {args.temporary_root}")
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
        validate_collect_files(args.collect_file)
        dos_drive = normalize_dos_drive(args.dos_drive)
        dos_drive_lower = dos_drive.lower()

        dosbox_version = command_version(dosbox)

        tokens = read_autotype(args.autotype)
        args.output.mkdir(parents=True, exist_ok=True)
        manifest_path = args.output / "manifest.json"
        video_path = args.output / "original.avi"
        log_path = args.output / "dosbox.log"
        for path in (manifest_path, video_path, log_path):
            if path.exists():
                raise RuntimeError(f"refusing to overwrite existing capture artifact: {path}")
        if any(args.output.glob("original-*.avi")):
            raise RuntimeError(
                f"refusing to reuse segmented capture artifacts in {args.output}")

        with tempfile.TemporaryDirectory(
                prefix="swd2-dosbox-", dir=args.temporary_root) as temporary:
            temporary_path = Path(temporary)
            dos_root = temporary_path / f"drive-{dos_drive_lower}"
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
                "-c", f"mount {dos_drive_lower} {dos_root}",
                "-c", f"{dos_drive_lower}:",
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
            # DOSBox-X's emulated -time-limit is the primary boundary, while
            # the host-side monitor also prevents malformed input or a CPU
            # fault from filling RAM/disk indefinitely.
            try:
                returncode, output = run_dosbox(
                    command, log_path, args.time_limit + 30)
                if returncode != 0:
                    raise RuntimeError(
                        f"DOSBox-X exited with status {returncode}; "
                        f"see {log_path}"
                    )
                videos = sorted(captures.glob("*.avi"))
                if not videos:
                    raise RuntimeError(
                        "DOSBox-X produced no AVI files; "
                        f"see {log_path}"
                    )
                if len(videos) != 1 and not args.allow_video_segments:
                    raise RuntimeError(
                        f"DOSBox-X produced {len(videos)} AVI files instead of one; "
                        "pass --allow-video-segments only after confirming the "
                        f"released mode-switch boundary; see {log_path}"
                    )
                collected_sources: list[tuple[Path, Path]] = []
                for name in args.collect_file:
                    source = dos_game / name
                    if not source.is_file():
                        raise RuntimeError(
                            f"requested DOS output was not produced: {name}")
                    destination = args.output / name
                    if destination.exists():
                        raise RuntimeError(
                            "refusing to overwrite collected artifact: "
                            f"{destination}")
                    collected_sources.append((source, destination))
            except RuntimeError:
                # A CPU fault or host-side timeout can leave a valid prefix in
                # DOSBox-X's still-open AVI.  A mapper error emitted while the
                # child is exiting can likewise happen only after one or more
                # finalized segments exist.  Preserve either kind of forensic
                # prefix before TemporaryDirectory removes the isolated
                # drive.  ``failed`` files never receive a reference manifest,
                # so an interrupted observation cannot be mistaken for golden
                # evidence.
                preserve_failed_videos(captures, args.output)
                raise
            video_paths: list[Path] = []
            for index, source in enumerate(videos):
                destination = (
                    video_path if len(videos) == 1 else
                    args.output / f"original-{index:03d}.avi"
                )
                shutil.move(source, destination)
                video_paths.append(destination)

            collected_paths: list[Path] = []
            for source, destination in collected_sources:
                shutil.copy2(source, destination)
                collected_paths.append(destination)

        video_artifacts: list[dict[str, object]] = []
        for path in video_paths:
            video_artifacts.append({
                "path": path.name,
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
                **video_metadata(ffprobe, path),
            })
        frame_artifacts: list[dict[str, object]] = []
        if args.extract_fps > 0:
            frames = args.output / "frames"
            if frames.exists():
                raise RuntimeError(f"refusing to reuse frame directory: {frames}")
            frames.mkdir()
            for segment, path in enumerate(video_paths):
                segment_frames = (
                    frames if len(video_paths) == 1 else
                    frames / f"segment-{segment:03d}"
                )
                segment_frames.mkdir(exist_ok=True)
                subprocess.run(
                    [
                        ffmpeg, "-v", "error", "-i", str(path),
                        "-vf",
                        f"fps={format(args.extract_fps, 'g')},"
                        "scale=320:200:flags=neighbor",
                        str(segment_frames / "%06d.png"),
                    ],
                    check=True,
                )
                for frame in sorted(segment_frames.glob("*.png")):
                    frame_artifacts.append({
                        "segment": segment,
                        "path": str(frame.relative_to(args.output)),
                        "bytes": frame.stat().st_size,
                        "sha256": sha256(frame),
                    })
            if not frame_artifacts:
                raise RuntimeError("DOSBox review-frame extraction produced no frames")

        autotype_bytes = (args.autotype.read_bytes()
                          if args.autotype is not None else b"")
        collected_artifacts = [
            {
                "path": path.name,
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
            for path in collected_paths
        ]
        manifest = {
            "schema_version": 1 if len(video_artifacts) == 1 else 2,
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
            "dos_drive": dos_drive,
            "game_path_layout": f"{dos_drive}:\\SWD2",
            "dos_date": "1994-08-02",
            "dos_time": "12:00:00",
            "autotype": {
                "source_sha256": hashlib.sha256(autotype_bytes).hexdigest(),
                "wait_seconds": args.wait,
                "pace_seconds": args.pace,
                "tokens": tokens,
            },
            "time_limit_seconds": args.time_limit,
            "review_extract_fps": args.extract_fps,
            "review_frames": frame_artifacts,
            "collected_files": collected_artifacts,
            "dosbox_exit_code": returncode,
        }
        if len(video_artifacts) == 1:
            manifest["video"] = video_artifacts[0]
        else:
            manifest["video_segments"] = video_artifacts
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        frames_captured = sum(
            int(item.get("nb_frames", 0)) for item in video_artifacts)
        print(
            "DOSBox-X original reference: "
            f"{len(video_artifacts)} video segment(s), "
            f"{frames_captured} frames"
        )
        print(f"Reference manifest: {manifest_path}")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"original reference capture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
