#!/usr/bin/env python3
"""Execute the Windows x86-64 CLI under pinned Wine and compare a native replay."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path


BASE_IMAGE = (
    "debian:13-slim@sha256:"
    "3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258"
)
IMAGE_TAG = "swd2-wine-x86_64:debian13"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(
    command: list[str], label: str, **kwargs: object
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, **kwargs)
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} exited {result.returncode}:\n{result.stdout[-4000:]}")
    return result


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--windows-executable", type=Path,
        default=Path("/tmp/swd2-windows-x86_64-build/src/swd2_rewrite.exe"))
    parser.add_argument(
        "--native-executable", type=Path,
        default=root / "build/src/swd2_rewrite")
    args = parser.parse_args()
    try:
        if shutil.which("docker") is None:
            raise ValueError("docker is required")
        try:
            output_relative = args.output.resolve().relative_to(root.resolve())
        except ValueError as error:
            raise ValueError(
                "Windows Wine checkpoint output must be inside the repository"
            ) from error
        # The cross-build runner has just replaced this checkpoint directory,
        # so those evidence files are expected to be dirty.  Refuse any source
        # or unrelated evidence edits rather than forcing an intermediate
        # commit whose revision no longer equals native-log.json.
        outside_status = checked([
            "git", "status", "--porcelain", "--", ".",
            f":(exclude){output_relative.as_posix()}",
            f":(exclude){output_relative.as_posix()}/**",
        ], "git status", cwd=root).stdout.strip()
        if outside_status:
            raise ValueError(
                "Windows Wine checkpoint has changes outside its output: "
                + outside_status)
        source_commit = checked(
            ["git", "rev-parse", "HEAD"], "git revision", cwd=root
        ).stdout.strip()
        source_tree = checked(
            ["git", "rev-parse", "HEAD^{tree}"], "git source tree", cwd=root
        ).stdout.strip()
        if not args.windows_executable.is_file():
            raise ValueError("Windows checkpoint executable is absent")
        if not args.native_executable.is_file():
            raise ValueError("native comparison executable is absent")

        build_record_path = args.output / "native-log.json"
        if not build_record_path.is_file():
            raise ValueError("Windows cross-build native-log.json is absent")
        build_record = json.loads(build_record_path.read_text(encoding="utf-8"))
        if build_record.get("source_tree") != source_tree or \
                build_record.get("executable_sha256") != sha256(
                    args.windows_executable):
            raise ValueError(
                "Windows executable does not match this clean checkpoint/source")

        checked([
            "docker", "build", "--platform", "linux/amd64",
            "-t", IMAGE_TAG,
            "-f", str(root /
                      "verification/platform/windows-x86_64/Dockerfile.wine"),
            str(root / "verification/platform/windows-x86_64"),
        ], "Windows Wine image build")
        image_id = checked([
            "docker", "image", "inspect", IMAGE_TAG,
            "--format", "{{.Id}}",
        ], "Windows Wine image inspection").stdout.strip()
        wine_version = checked([
            "docker", "run", "--rm", "--platform", "linux/amd64",
            IMAGE_TAG, "/usr/lib/wine/wine64", "--version",
        ], "Wine version").stdout.strip()
        container_machine = checked([
            "docker", "run", "--rm", "--platform", "linux/amd64",
            IMAGE_TAG, "uname", "-m",
        ], "Wine container architecture").stdout.strip()
        if container_machine != "x86_64" or not wine_version.startswith("wine-10."):
            raise ValueError("Wine container identity differs")

        with tempfile.TemporaryDirectory(prefix="swd2-windows-wine-") as raw:
            work = Path(raw)
            native_trace = work / "native-trace.json"
            windows_trace = work / "windows-trace.json"
            native_frames = work / "native-frames.bin"
            windows_frames = work / "windows-frames.bin"
            native_log = checked([
                str(args.native_executable),
                "--game", str(root / "game"),
                "--save-dir", str(work / "native-saves"),
                "--no-save", "--run-replay",
                str(root / "scripts/replay-smoke.txt"),
                "--trace-output", str(native_trace),
                "--frame-output", str(native_frames),
            ], "native replay")
            wine_command = (
                "mkdir -p \"$XDG_RUNTIME_DIR\"; "
                "/usr/lib/wine/wine64 /windows/swd2_rewrite.exe "
                "--game Z:\\\\source\\\\game "
                "--save-dir Z:\\\\output\\\\windows-saves --no-save "
                "--run-replay Z:\\\\source\\\\scripts\\\\replay-smoke.txt "
                "--trace-output Z:\\\\output\\\\windows-trace.json "
                "--frame-output Z:\\\\output\\\\windows-frames.bin"
            )
            wine_log = checked([
                "docker", "run", "--rm", "--platform", "linux/amd64",
                "-v", f"{root}:/source:ro",
                "-v", f"{args.windows_executable.parent}:/windows:ro",
                "-v", f"{work}:/output",
                IMAGE_TAG, "sh", "-lc", wine_command,
            ], "Windows PE replay under Wine")
            for trace in (native_trace, windows_trace):
                checked([
                    "python3", str(root / "scripts/verify-replay-trace.py"),
                    str(trace), "--min-frames", "113",
                ], "replay trace verification")
            comparison_path = work / "comparison.json"
            checked([
                "python3", str(root / "scripts/compare-replay-traces.py"),
                str(native_trace), str(windows_trace),
                "--output", str(comparison_path),
            ], "native/Windows replay comparison")
            if native_frames.read_bytes() != windows_frames.read_bytes():
                raise ValueError("native and Windows frame captures differ")

            trace = json.loads(windows_trace.read_text(encoding="utf-8"))
            args.output.mkdir(parents=True, exist_ok=True)
            for source, name in (
                (native_trace, "wine-native-trace.json"),
                (windows_trace, "wine-windows-trace.json"),
                (comparison_path, "wine-comparison.json"),
            ):
                shutil.copyfile(source, args.output / name)
            (args.output / "wine-native-run.log").write_text(
                native_log.stdout, encoding="utf-8")
            (args.output / "wine-windows-run.log").write_text(
                wine_log.stdout, encoding="utf-8")
            execution = {
                "schema_version": 1,
                "kind": "swd2_windows_wine_execution_checkpoint",
                "status": "verified",
                "scope": "emulated_not_windows_host",
                "source_commit": source_commit,
                "source_tree": source_tree,
                "host_system": platform.system(),
                "host_architecture": platform.machine(),
                "container_base": BASE_IMAGE,
                "container_image_id": image_id,
                "container_architecture": container_machine,
                "wine_version": wine_version,
                "windows_executable_sha256": sha256(args.windows_executable),
                "native_trace_sha256": sha256(args.output /
                                               "wine-native-trace.json"),
                "windows_trace_sha256": sha256(args.output /
                                                "wine-windows-trace.json"),
                "comparison_sha256": sha256(args.output /
                                             "wine-comparison.json"),
                "native_run_log_sha256": sha256(args.output /
                                                 "wine-native-run.log"),
                "windows_run_log_sha256": sha256(args.output /
                                                  "wine-windows-run.log"),
                "frame_capture_sha256": sha256(windows_frames),
                "frame_capture_bytes": windows_frames.stat().st_size,
                "exact_native_frame_capture_match": True,
                "replay": {
                    "frames": trace["video"]["frames"],
                    "inputs": trace["input"]["consumed"],
                    "transitions": len(trace["transitions"]),
                    "final_state_fnv1a64": trace["state_fnv1a64"],
                    "final_mapz_fnv1a64": trace["mapz_fnv1a64"],
                    "video_fnv1a64": trace["video"]["fnv1a64"],
                    "audio_fnv1a64": trace["audio"]["fnv1a64"],
                },
                "limitation": (
                    "The AMD64 Windows PE executed through Wine 10 inside an "
                    "x86_64 Docker container. This proves deterministic PE/CRT "
                    "execution but is not a physical Windows-host or SDL test."
                ),
            }
            execution_path = args.output / "wine-execution-log.json"
            write_json(execution_path, execution)
            matrix_path = args.output / "matrix-report.json"
            matrix = {
                "schema_version": 1,
                "kind": "swd2_windows_platform_checkpoint",
                "status": "in_progress",
                "coverage": {
                    "platform": "windows",
                    "architecture": "x86_64",
                    "native_release_cross_builds": 1,
                    "pe_cli_outputs": 1,
                    "wine_pe_replays": 1,
                    "native_equivalence_replays": 1,
                    "exact_indexed_frame_matches":
                        trace["video"]["frames"],
                },
                "artifacts": {
                    "native_log_sha256": sha256(build_record_path),
                    "build_log_sha256": sha256(args.output / "build.log"),
                    "wine_execution_log_sha256": sha256(execution_path),
                    "wine_native_trace_sha256": sha256(args.output /
                                                        "wine-native-trace.json"),
                    "wine_windows_trace_sha256": sha256(args.output /
                                                         "wine-windows-trace.json"),
                    "wine_comparison_sha256": sha256(args.output /
                                                      "wine-comparison.json"),
                },
                "remaining": [
                    "physical Windows-hosted execution and deterministic replay",
                    "Windows SDL2 display/input/audio frontend build and run",
                    "multi-hour active Windows gameplay/audio soak",
                    "physical gamepad matrix",
                ],
            }
            write_json(matrix_path, matrix)

        print(
            "Windows Wine checkpoint: 113-frame PE replay is byte-exact "
            "with native output (emulated, not a Windows-host claim)")
        return 0
    except (OSError, ValueError, RuntimeError, KeyError, TypeError,
            subprocess.SubprocessError, json.JSONDecodeError) as error:
        parser.exit(1, f"Windows Wine checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
