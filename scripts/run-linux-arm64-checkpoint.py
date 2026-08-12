#!/usr/bin/env python3
"""Build and test the native rewrite in a pinned Linux ARM64 container."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path


BASE_IMAGE = (
    "debian@sha256:3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258"
)
IMAGE_TAG = "swd2-linux-arm64-checkpoint:debian13"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(command: list[str], label: str, **kwargs) -> subprocess.CompletedProcess:
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
    parser.add_argument(
        "--output", type=Path, required=True,
        help="repository evidence directory to replace")
    parser.add_argument(
        "--build", type=Path, default=Path("/tmp/swd2-linux-arm64-build"))
    args = parser.parse_args()
    try:
        if shutil.which("docker") is None:
            raise ValueError("docker is required")
        if checked(["git", "status", "--porcelain"], "git status",
                   cwd=root).stdout.strip():
            raise ValueError("Linux checkpoint requires a clean worktree")
        source_commit = checked(
            ["git", "rev-parse", "HEAD"], "git revision", cwd=root
        ).stdout.strip()

        checked([
            "docker", "build", "--platform", "linux/arm64",
            "-t", IMAGE_TAG,
            "-f", str(root / "verification/platform/linux-arm64/Dockerfile"),
            str(root / "verification/platform/linux-arm64"),
        ], "Linux checkpoint image build")
        image_id = checked([
            "docker", "image", "inspect", IMAGE_TAG,
            "--format", "{{.Id}}",
        ], "Linux checkpoint image inspection").stdout.strip()

        # Keep the previous committed checkpoint visible while its own CTest
        # verifier runs inside the read-only source mount. Replace it only
        # after the new build/test has completed successfully.
        if args.build.exists():
            shutil.rmtree(args.build)
        args.build.mkdir(parents=True)
        command = (
            "set -e; "
            "cmake --version; c++ --version; python3 --version; "
            "cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release; "
            "cmake --build /build -j2; "
            "SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "
            "SDL_RENDER_DRIVER=software "
            "ctest --test-dir /build --output-on-failure -j2"
        )
        result = checked([
            "docker", "run", "--rm", "--platform", "linux/arm64",
            "-v", f"{root}:/src:ro",
            "-v", f"{args.build}:/build",
            IMAGE_TAG, "bash", "-lc", command,
        ], "Linux ARM64 build/test")
        if args.output.exists():
            shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        log_path = args.output / "ctest.log"
        log_path.write_text(result.stdout, encoding="utf-8")

        match = re.search(
            r"100% tests passed, 0 tests failed out of ([0-9]+)",
            result.stdout)
        compiler = re.search(
            r"The CXX compiler identification is ([^\n]+)", result.stdout)
        if match is None or compiler is None:
            raise ValueError("Linux test/compiler summary is absent")
        warning_count = len(re.findall(r"warning:", result.stdout))
        if warning_count != 0:
            raise ValueError(f"Linux build emitted {warning_count} warnings")
        test_count = int(match.group(1))
        executable = args.build / "src/swd2_rewrite"
        if not executable.is_file():
            raise ValueError("Linux executable is absent")

        native = {
            "schema_version": 1,
            "kind": "swd2_linux_native_checkpoint",
            "status": "verified",
            "source_commit": source_commit,
            "container_base": BASE_IMAGE,
            "container_image_id": image_id,
            "platform": "linux",
            "architecture": "aarch64",
            "compiler": compiler.group(1).strip(),
            "build_type": "Release",
            "warning_count": warning_count,
            "tests_passed": test_count,
            "tests_failed": 0,
            "ctest_log_sha256": sha256(log_path),
            "executable_sha256": sha256(executable),
        }
        native_path = args.output / "native-log.json"
        write_json(native_path, native)
        matrix = {
            "schema_version": 1,
            "kind": "swd2_linux_platform_checkpoint",
            "status": "in_progress",
            "coverage": {
                "platform": "linux",
                "architecture": "aarch64",
                "native_release_builds": 1,
                "native_ctest_passes": test_count,
            },
            "artifacts": {
                "native_log_sha256": sha256(native_path),
                "ctest_log_sha256": sha256(log_path),
            },
            "remaining": [
                "multi-hour active Linux gameplay/audio soak",
                "physical Linux desktop SDL/input/audio run",
                "physical Windows native SDL/input/audio backend",
                "physical iOS/Android browser and gamepad matrix",
            ],
        }
        write_json(args.output / "matrix-report.json", matrix)
        print(
            f"Linux ARM64 checkpoint: {test_count}/{test_count} tests, "
            "zero compiler warnings")
        return 0
    except (OSError, ValueError, RuntimeError, TypeError,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"Linux ARM64 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
