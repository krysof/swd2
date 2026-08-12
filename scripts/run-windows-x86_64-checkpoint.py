#!/usr/bin/env python3
"""Cross-build the portable core/CLI as a Windows x86-64 PE checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path


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


def pe_identity(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("Windows output has no DOS/PE header")
    pe_offset = struct.unpack_from("<I", data, 0x3c)[0]
    if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("Windows output has no PE signature")
    machine, sections = struct.unpack_from("<HH", data, pe_offset + 4)
    characteristics = struct.unpack_from("<H", data, pe_offset + 22)[0]
    return machine, sections, characteristics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--build", type=Path,
        default=Path("/tmp/swd2-windows-x86_64-build"))
    args = parser.parse_args()
    try:
        compiler = shutil.which("x86_64-w64-mingw32-c++")
        if compiler is None:
            raise ValueError("x86_64-w64-mingw32-c++ is required")
        if shutil.which("cmake") is None or shutil.which("ninja") is None:
            raise ValueError("cmake and ninja are required")
        objdump = shutil.which("x86_64-w64-mingw32-objdump")
        if objdump is None:
            raise ValueError("x86_64-w64-mingw32-objdump is required")
        if checked(["git", "status", "--porcelain"], "git status",
                   cwd=root).stdout.strip():
            raise ValueError("Windows checkpoint requires a clean worktree")
        source_commit = checked(
            ["git", "rev-parse", "HEAD"], "git revision", cwd=root
        ).stdout.strip()
        target = checked(
            [compiler, "-dumpmachine"], "compiler target").stdout.strip()
        if target != "x86_64-w64-mingw32":
            raise ValueError(f"unexpected compiler target: {target}")
        compiler_version = checked(
            [compiler, "--version"], "compiler version").stdout.splitlines()[0]

        for path in (args.output, args.build):
            if path.exists():
                shutil.rmtree(path)
            path.mkdir(parents=True)
        configure = checked([
            "cmake", "-S", str(root), "-B", str(args.build), "-G", "Ninja",
            "-DCMAKE_SYSTEM_NAME=Windows",
            f"-DCMAKE_CXX_COMPILER={compiler}",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTING=OFF",
            "-DSWD2_NATIVE_SDL2=OFF",
        ], "Windows CMake configure")
        build = checked(
            ["cmake", "--build", str(args.build), "-j2"],
            "Windows cross build")
        build_log = configure.stdout + build.stdout
        warning_count = len(re.findall(r"warning:", build_log, re.IGNORECASE))
        if warning_count != 0:
            raise ValueError(
                f"Windows cross build emitted {warning_count} warnings")
        log_path = args.output / "build.log"
        log_path.write_text(build_log, encoding="utf-8")

        executable = args.build / "src" / "swd2_rewrite.exe"
        if not executable.is_file():
            raise ValueError("Windows swd2_rewrite.exe is absent")
        machine, sections, characteristics = pe_identity(executable)
        if machine != 0x8664 or sections == 0 or (characteristics & 0x0002) == 0:
            raise ValueError("Windows output is not an executable AMD64 PE")
        pe_dump = checked([objdump, "-p", str(executable)], "PE import inspection")
        imports = sorted(set(re.findall(r"DLL Name: ([^\r\n]+)", pe_dump.stdout)))
        forbidden_runtime_imports = [
            name for name in imports
            if name.lower().startswith(("libgcc_", "libstdc++-", "libwinpthread-"))
        ]
        if forbidden_runtime_imports:
            raise ValueError(
                "Windows output still requires adjacent MinGW runtime DLLs: "
                + ", ".join(forbidden_runtime_imports))

        native = {
            "schema_version": 1,
            "kind": "swd2_windows_cross_build_checkpoint",
            "status": "verified",
            "source_commit": source_commit,
            "compiler": compiler_version,
            "compiler_target": target,
            "build_type": "Release",
            "sdl_frontend": "disabled_for_cross_build",
            "mingw_runtime": "statically_linked",
            "pe_imports": imports,
            "warning_count": warning_count,
            "pe_machine": "0x8664",
            "pe_sections": sections,
            "pe_characteristics": f"0x{characteristics:04x}",
            "executable_size": executable.stat().st_size,
            "executable_sha256": sha256(executable),
            "build_log_sha256": sha256(log_path),
        }
        native_path = args.output / "native-log.json"
        write_json(native_path, native)
        matrix = {
            "schema_version": 1,
            "kind": "swd2_windows_platform_checkpoint",
            "status": "in_progress",
            "coverage": {
                "platform": "windows",
                "architecture": "x86_64",
                "native_release_cross_builds": 1,
                "pe_cli_outputs": 1,
            },
            "artifacts": {
                "native_log_sha256": sha256(native_path),
                "build_log_sha256": sha256(log_path),
            },
            "remaining": [
                "Windows-hosted execution and deterministic replay",
                "Windows SDL2 display/input/audio frontend build and run",
                "multi-hour active Windows gameplay/audio soak",
                "physical gamepad matrix",
            ],
        }
        write_json(args.output / "matrix-report.json", matrix)
        print(
            "Windows x86-64 checkpoint: cross-build OK "
            f"({executable.stat().st_size} byte PE, zero warnings)")
        return 0
    except (OSError, ValueError, RuntimeError, TypeError,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"Windows x86-64 checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
