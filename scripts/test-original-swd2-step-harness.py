#!/usr/bin/env python3
"""Assemble and hash-lock the isolated original RPG/FIG step harnesses."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path


EXPECTED = {
    "RPG_STEP": (
        242,
        "e169d08e8bf30e8a06597c62897ea453f9edbf6eca09f5f7c65f461767ba8bc1",
    ),
    "RPG_TITLE_STEP": (
        242,
        "626132a2380464aad548b300384004f51d91ba963cf16689e0638d3709cfb6c7",
    ),
    "FIG_STEP": (
        298,
        "b5ef8838d97cfe81882d4498559c07e4496f0e7c71443bc99b1a9cfe9c49ac0f",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nasm", type=Path)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    if not args.nasm.is_file():
        parser.error(f"NASM does not exist: {args.nasm}")
    if not args.source.is_file():
        parser.error(f"step harness source does not exist: {args.source}")

    with tempfile.TemporaryDirectory(prefix="swd2-step-harness-test-") as temporary:
        root = Path(temporary)
        for define, (expected_size, expected_sha256) in EXPECTED.items():
            output = root / f"{define}.COM"
            subprocess.run([
                str(args.nasm), "-f", "bin", f"-d{define}=1",
                str(args.source), "-o", str(output),
            ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            data = output.read_bytes()
            actual = hashlib.sha256(data).hexdigest()
            if len(data) != expected_size or actual != expected_sha256:
                raise RuntimeError(
                    f"{define} harness differs: bytes={len(data)}, sha256={actual}")

        phase_output = root / "RPG_PHASE_11.COM"
        subprocess.run([
            str(args.nasm), "-f", "bin", "-dRPG_STEP=1",
            "-dRPG_PHASE_HUNDREDTH=11", str(args.source),
            "-o", str(phase_output),
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        phase_data = phase_output.read_bytes()
        phase_sha256 = hashlib.sha256(phase_data).hexdigest()
        if len(phase_data) != 256 or phase_sha256 != \
                "071334d04deb6c147b4279f00cd1fb3c5428982144d1df0aa2d17b15cb0f7471":
            raise RuntimeError(
                "RPG phase-11 harness differs: "
                f"bytes={len(phase_data)}, sha256={phase_sha256}")

        fig_phase_output = root / "FIG_PHASE_00.COM"
        subprocess.run([
            str(args.nasm), "-f", "bin", "-dFIG_STEP=1",
            "-dFIG_PHASE_HUNDREDTH=0", str(args.source),
            "-o", str(fig_phase_output),
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        fig_phase_data = fig_phase_output.read_bytes()
        fig_phase_sha256 = hashlib.sha256(fig_phase_data).hexdigest()
        if len(fig_phase_data) != 308 or fig_phase_sha256 != \
                "75aaa6b245c3770a828adb636e7f25f1bd3e0068197f929336ebff4f2cc4e8b1":
            raise RuntimeError(
                "FIG phase-00 harness differs: "
                f"bytes={len(fig_phase_data)}, sha256={fig_phase_sha256}")

        fig_clock_output = root / "FIG_CLOCK_00.COM"
        subprocess.run([
            str(args.nasm), "-f", "bin", "-dFIG_STEP=1",
            "-dFIG_FIXED_HUNDREDTH=0", str(args.source),
            "-o", str(fig_clock_output),
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        fig_clock_data = fig_clock_output.read_bytes()
        fig_clock_sha256 = hashlib.sha256(fig_clock_data).hexdigest()
        if len(fig_clock_data) != 368 or fig_clock_sha256 != \
                "5cd8f7e75242b096454192b61c1287cb5dfe1600617f7fe63294c8516963750f":
            raise RuntimeError(
                "FIG fixed-clock harness differs: "
                f"bytes={len(fig_clock_data)}, sha256={fig_clock_sha256}")

        for selector, filename, expected_sha256 in (
                ("RPG_STEP", "RPG_CLOCK_00.COM",
                 "7c180a167e82a2d7f155ad56960006eb82e7017795cd4cd962e0c08abd130753"),
                ("RPG_TITLE_STEP", "RPG_TITLE_CLOCK_00.COM",
                 "e3ff5624ca8bd7d05620020b209a0443e0385e272c3b03e8207b455b19947629")):
            rpg_clock_output = root / filename
            subprocess.run([
                str(args.nasm), "-f", "bin", f"-d{selector}=1",
                "-dRPG_FIXED_HUNDREDTH=0", str(args.source),
                "-o", str(rpg_clock_output),
            ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            rpg_clock_data = rpg_clock_output.read_bytes()
            rpg_clock_sha256 = hashlib.sha256(rpg_clock_data).hexdigest()
            if len(rpg_clock_data) != 376 or \
                    rpg_clock_sha256 != expected_sha256:
                raise RuntimeError(
                    f"{selector} fixed-clock harness differs: "
                    f"bytes={len(rpg_clock_data)}, "
                    f"sha256={rpg_clock_sha256}")

        # The source must fail closed unless exactly one child contract is
        # selected.  Otherwise a typo could silently build a third, untracked
        # program while still looking like valid 16-bit machine code.
        selectors = tuple(EXPECTED)
        invalid_selectors = [()]
        invalid_selectors.extend(
            (left, right)
            for offset, left in enumerate(selectors)
            for right in selectors[offset + 1:])
        invalid_selectors.append(selectors)
        for defines in invalid_selectors:
            command = [str(args.nasm), "-f", "bin"]
            command.extend(f"-d{define}=1" for define in defines)
            command.extend([str(args.source), "-o", str(root / "invalid.com")])
            result = subprocess.run(
                command, check=False, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            if result.returncode == 0:
                raise RuntimeError(
                    "step harness accepted invalid selector set: " +
                    repr(defines))
        for extra in ("-dFIG_STEP=1", "-dRPG_TITLE_STEP=1",
                      "-dRPG_PHASE_HUNDREDTH=100"):
            result = subprocess.run([
                str(args.nasm), "-f", "bin", "-dRPG_PHASE_HUNDREDTH=11"
                if extra != "-dRPG_PHASE_HUNDREDTH=100" else "-dRPG_STEP=1",
                extra, str(args.source), "-o", str(root / "invalid-phase.com"),
            ], check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode == 0:
                raise RuntimeError(
                    "step harness accepted invalid phase selector: " + extra)
        for selector, extra in (
                ("-dRPG_STEP=1", "-dFIG_PHASE_HUNDREDTH=0"),
                ("-dRPG_TITLE_STEP=1", "-dFIG_PHASE_HUNDREDTH=0"),
                ("-dFIG_STEP=1", "-dFIG_PHASE_HUNDREDTH=100"),
                ("-dFIG_STEP=1", "-dRPG_PHASE_HUNDREDTH=0")):
            result = subprocess.run([
                str(args.nasm), "-f", "bin", selector, extra,
                str(args.source), "-o", str(root / "invalid-fig-phase.com"),
            ], check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode == 0:
                raise RuntimeError(
                    "step harness accepted invalid FIG phase selector: " +
                    selector + " " + extra)
        for selector, extra in (
                ("-dRPG_STEP=1", "-dFIG_FIXED_HUNDREDTH=0"),
                ("-dRPG_TITLE_STEP=1", "-dFIG_FIXED_HUNDREDTH=0"),
                ("-dFIG_STEP=1", "-dFIG_FIXED_HUNDREDTH=100"),
                ("-dFIG_PHASE_HUNDREDTH=0", "-dFIG_FIXED_HUNDREDTH=0")):
            command = [str(args.nasm), "-f", "bin"]
            if selector.startswith("-dFIG_PHASE"):
                command.append("-dFIG_STEP=1")
            command.extend((selector, extra, str(args.source), "-o",
                            str(root / "invalid-fig-clock.com")))
            result = subprocess.run(
                command, check=False, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            if result.returncode == 0:
                raise RuntimeError(
                    "step harness accepted invalid FIG fixed-clock selector: " +
                    selector + " " + extra)
        for selector, extra in (
                ("-dFIG_STEP=1", "-dRPG_FIXED_HUNDREDTH=0"),
                ("-dRPG_STEP=1", "-dRPG_FIXED_HUNDREDTH=100"),
                ("-dRPG_TITLE_STEP=1", "-dRPG_FIXED_HUNDREDTH=100"),
                ("-dRPG_PHASE_HUNDREDTH=0", "-dRPG_FIXED_HUNDREDTH=0"),
                ("-dFIG_FIXED_HUNDREDTH=0", "-dRPG_FIXED_HUNDREDTH=0")):
            command = [str(args.nasm), "-f", "bin"]
            if selector.startswith("-dRPG_PHASE"):
                command.append("-dRPG_STEP=1")
            elif selector.startswith("-dFIG_FIXED"):
                command.append("-dFIG_STEP=1")
            command.extend((selector, extra, str(args.source), "-o",
                            str(root / "invalid-rpg-clock.com")))
            result = subprocess.run(
                command, check=False, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            if result.returncode == 0:
                raise RuntimeError(
                    "step harness accepted invalid RPG fixed-clock selector: " +
                    selector + " " + extra)

    print(
        "original step harness: RPG direct/title-load transfers and FIG "
        "SAVE.DAQ reconstruction plus phase-controlled RPG/FIG probes "
        "and fixed-clock RPG/FIG probes assemble to locked binaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
