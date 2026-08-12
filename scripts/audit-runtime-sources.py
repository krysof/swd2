#!/usr/bin/env python3
"""Fail closed when production sources regain porting placeholders.

This is deliberately narrower than the playthrough and pixel-difference gates:
it proves that every portable core translation unit is built and that the
production implementation does not contain an explicitly marked stub, dead
preprocessor block, DOS-emulator dependency, or child-process escape hatch.
Reachability and behavioural parity remain separate completion requirements.
"""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
CORE_CMAKE = SRC / "CMakeLists.txt"


def fail(message: str) -> None:
    raise RuntimeError(message)


def core_translation_units(cmake_text: str) -> set[str]:
    match = re.search(
        r"add_library\(swd2_core\s+(.*?)\n\)", cmake_text, re.DOTALL
    )
    if match is None:
        fail("src/CMakeLists.txt has no literal swd2_core source list")
    units = {
        token
        for token in re.findall(r"(?m)^\s*([A-Za-z0-9_./-]+\.cpp)\s*$", match.group(1))
    }
    if not units:
        fail("swd2_core source list is empty")
    return units


def main() -> int:
    cmake_text = CORE_CMAKE.read_text(encoding="utf-8")
    configured = core_translation_units(cmake_text)
    expected = {
        path.name
        for path in SRC.glob("*.cpp")
        if path.name not in {"main.cpp", "sdl_platform.cpp"}
    }
    missing = sorted(expected - configured)
    stale = sorted(configured - expected)
    if missing or stale:
        fail(
            "swd2_core translation-unit inventory differs from src/: "
            f"missing={missing}, stale={stale}"
        )

    production_paths = sorted(SRC.glob("*.cpp")) + sorted((SRC / "include").rglob("*.hpp"))
    placeholder = re.compile(
        r"\b(?:TODO|FIXME|XXX|STUB|PLACEHOLDER|NOT[ _-]IMPLEMENTED|"
        r"APPROXIMAT(?:E|ED|ES|ING|ION|IONS))\b",
        re.IGNORECASE,
    )
    disabled_code = re.compile(r"(?m)^\s*#\s*if\s+0(?:\s|$)")
    emulator = re.compile(r"\b(?:dosbox|dosemu)\b", re.IGNORECASE)
    process_escape = re.compile(
        r"\b(?:std::system|system|popen|_popen|fork|posix_spawn|CreateProcess[A-Z]?|"
        r"ShellExecute[A-Z]?|WinExec)\s*\("
    )
    exec_escape = re.compile(r"\bexec(?:l|le|lp|lpe|v|ve|vp|vpe)\s*\(")

    violations: list[str] = []
    for path in production_paths:
        text = path.read_text(encoding="utf-8")
        for label, pattern in (
            ("placeholder marker", placeholder),
            ("disabled #if 0 block", disabled_code),
            ("DOS emulator dependency", emulator),
            ("child-process API", process_escape),
            ("exec-family API", exec_escape),
        ):
            match = pattern.search(text)
            if match is not None:
                line = text.count("\n", 0, match.start()) + 1
                violations.append(f"{path.relative_to(ROOT)}:{line}: {label}")

    if violations:
        fail("production runtime static audit failed:\n  " + "\n  ".join(violations))

    modules = {"meo_module.cpp", "rpg_module.cpp", "battle_module.cpp", "demo_module.cpp"}
    if not modules <= configured:
        fail(f"single-process module set is incomplete: {sorted(modules - configured)}")
    if "sdl_platform.cpp" in configured:
        fail("SDL platform shell leaked into the platform-independent swd2_core")

    print(
        "runtime source audit: OK "
        f"({len(configured)} core translation units, {len(production_paths)} production files)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"runtime source audit: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
