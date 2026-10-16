#!/usr/bin/env -S uv run --script
"""Which CI jobs a change needs (obs-rust/obs-studio#436).

    uv run ci/ci_scope.py <base-ref>        # writes core/macos/windows to $GITHUB_OUTPUT

The Linux Rust tests and Tier 2 always run. On top of them:
- macos:   macOS code (or shared build/CI config) changed -> macOS Rust
           tests and Tier 2;
- windows: Windows code (or shared build/CI config) changed -> Windows Rust
           tests and Tier 2;
- core:    libobs or the Rust workspace changed (informational).
Platform is decided by path alone (#897, ci/platform_paths.py): a changed
file of a platform (`win-capture/`, `platform-osx.mm`, `threading-posix.c`,
`rust/*/src/platform/windows/`, ...) starts that platform's jobs. Core files
carry no platform defines (ci/platform_ifdef_ratchet.py), so a core change
runs on Linux. `.github/`, `cmake/common/`, `cmake/finders/`, `libobs/cmake/`,
the root CMake files, `platform/mod.rs` and the C++ cmocka tests (with their
tests.d registrations) stay shared: every platform consumes them.
Every OBS app build (Ubuntu, macOS, Windows, Flatpak) runs only in the
release gate (`full`). A job that is skipped here can't hide a break: it
runs at the release gate.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from platform_paths import MACOS, WINDOWS, platforms

CORE_PREFIXES = ("libobs/", "rust/")
CORE_FILES = {"Cargo.toml", "Cargo.lock", "rust-toolchain.toml"}
# Build and CI configuration every platform consumes.
SHARED_PREFIXES = (".github/", "cmake/common/", "cmake/finders/", "libobs/cmake/")
SHARED_FILES = {"CMakeLists.txt", "CMakePresets.json", "buildspec.json"}
SHARED_PATTERN = re.compile(r"^rust/[^/]+/src/platform/mod\.rs$")
# Every platform's Tier 2 compiles these with its own compiler and C++
# library; plain C tests stay Linux-only (#610).
SHARED_TEST = re.compile(r"^test/cmocka/(CMakeLists\.txt|[^/]+\.(cpp|hpp))$")
# A tests.d registration is shared when it registers a C++ test.
TESTS_D = re.compile(r"^test/cmocka/tests\.d/([^/]+)\.cmake$")


@dataclass(frozen=True)
class Scope:
    core: bool
    macos: bool
    windows: bool


def is_shared(path: str, exists: Callable[[str], bool]) -> bool:
    if path.startswith(SHARED_PREFIXES) or path in SHARED_FILES or SHARED_PATTERN.match(path) or SHARED_TEST.match(path):
        return True
    m = TESTS_D.match(path)
    return bool(m) and any(exists(f"test/cmocka/{m.group(1)}.{ext}") for ext in ("cpp", "hpp"))


def scope(files: list[str], exists: Callable[[str], bool] = lambda path: False) -> Scope:
    """`exists(path)` says whether a file exists in the checkout."""
    core = macos = windows = False
    for path in files:
        if path.startswith(CORE_PREFIXES) or path in CORE_FILES:
            core = True
        if is_shared(path, exists):
            macos = windows = True
            continue
        found = platforms(path) or frozenset()
        macos |= MACOS in found
        windows |= WINDOWS in found
    return Scope(core, macos, windows)


def changed_files(base: str) -> list[str]:
    out = subprocess.run(
        ["git", "diff", "--name-only", f"{base}...HEAD"], check=True, capture_output=True, text=True
    ).stdout
    return [line for line in out.splitlines() if line]


def main() -> int:
    files = changed_files(sys.argv[1])
    result = scope(files, lambda path: Path(path).is_file())
    lines = [f"{k}={str(v).lower()}" for k, v in vars(result).items()]
    print("\n".join(lines))
    if out := os.environ.get("GITHUB_OUTPUT"):
        with open(out, "a") as f:
            f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
