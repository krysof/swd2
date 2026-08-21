#!/usr/bin/env python3
"""Unit-check the failure-closed DOSBox-X AUTOTYPE log boundary."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
from pathlib import Path


def main() -> int:
    source = Path(__file__).with_name("capture-original-dosbox.py")
    spec = importlib.util.spec_from_file_location("capture_original_dosbox", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load capture-original-dosbox.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    module.validate_autotype_log(
        "LOG: Mapper keyboard layout is now us (US English)\n"
        "LOG: Started capturing video\nLOG: Stopped capturing video.\n")
    rejected = False
    try:
        module.validate_autotype_log(
            "LOG: Stopped capturing video.\n"
            "Message was: MAPPER: Couldn't find a button named 'right', "
            "stopping.\n")
    except RuntimeError as error:
        rejected = "AUTOTYPE stream" in str(error) and "right" in str(error)
    if not rejected:
        raise RuntimeError("invalid AUTOTYPE mapper token was accepted")

    runtime_rejected = False
    try:
        module.validate_runtime_log(
            "LOG: Stopped capturing video.\n"
            "ERROR CPU:Illegal Unhandled Interrupt Called 6\n")
    except RuntimeError as error:
        runtime_rejected = "fatal runtime boundary" in str(error)
    if not runtime_rejected:
        raise RuntimeError("fatal DOSBox-X CPU boundary was accepted")

    old_limit = module.MAX_LOG_BYTES
    module.MAX_LOG_BYTES = 8
    try:
        oversized_rejected = False
        try:
            module.validate_runtime_log("123456789")
        except RuntimeError as error:
            oversized_rejected = "evidence limit" in str(error)
        if not oversized_rejected:
            raise RuntimeError("oversized DOSBox-X diagnostic log was accepted")
    finally:
        module.MAX_LOG_BYTES = old_limit

    if module.normalize_dos_drive("e") != "E" or \
            module.normalize_dos_drive("C") != "C":
        raise RuntimeError("valid DOS drive letters were not normalized")
    for value in ("", "CC", "9", "E:", "../E"):
        drive_rejected = False
        try:
            module.normalize_dos_drive(value)
        except ValueError as error:
            drive_rejected = "ASCII drive letter" in str(error)
        if not drive_rejected:
            raise RuntimeError(f"unsafe DOS drive was accepted: {value!r}")

    for names, expected in (
            (["STEP.OUT", "step.out"], "unique"),
            (["manifest.json"], "capture artifact"),
            (["../STEP.OUT"], "plain DOS filename")):
        collect_rejected = False
        try:
            module.validate_collect_files(names)
        except ValueError as error:
            collect_rejected = expected in str(error)
        if not collect_rejected:
            raise RuntimeError(f"invalid collected file set was accepted: {names}")

    with tempfile.TemporaryDirectory(prefix="swd2-capture-log-test-") as temporary:
        root = Path(temporary)
        captures = root / "captures"
        output = root / "output"
        captures.mkdir()
        output.mkdir()
        (captures / "rpg_001.avi").write_bytes(b"second")
        (captures / "rpg_000.avi").write_bytes(b"first")
        preserved = module.preserve_failed_videos(captures, output)
        if [path.name for path in preserved] != [
                "failed-000.avi", "failed-001.avi"]:
            raise RuntimeError("failed capture segments were not sorted and retained")
        if [path.read_bytes() for path in preserved] != [b"first", b"second"]:
            raise RuntimeError("failed capture segment bytes changed")
        overwrite_rejected = False
        try:
            module.preserve_failed_videos(captures, output)
        except RuntimeError as error:
            overwrite_rejected = "overwrite" in str(error)
        if not overwrite_rejected:
            raise RuntimeError("failed capture evidence could be overwritten")

        # A child can print a fatal line and exit before the live polling loop
        # observes the final bytes.  run_dosbox must still reject the complete
        # log after wait() rather than returning it as successful evidence.
        final_log = root / "final-runtime.log"
        final_rejected = False
        try:
            module.run_dosbox([
                sys.executable, "-c",
                "print('ERROR CPU:Illegal Unhandled Interrupt Called 6')",
            ], final_log, 5)
        except RuntimeError as error:
            final_rejected = "Illegal Unhandled Interrupt" in str(error)
        if not final_rejected:
            raise RuntimeError("final DOSBox-X runtime fault escaped log validation")
    print(
        "DOSBox-X capture log: AUTOTYPE/CPU faults, oversized logs, "
        "DOS-drive injection and collected-file collisions fail closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
