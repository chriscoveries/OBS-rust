#!/usr/bin/env -S uv run --script
"""The Rust staticlib is built for the macOS architecture CMake targets.

    uv run python -m unittest ci/test_rust_cargo_target.py

Corrosion defaults to the host triple. The release's macOS x86_64 leg is
cross-built on an arm64 runner (CMAKE_OSX_ARCHITECTURES=x86_64), so libobs
linked an aarch64 libobs_rust and failed with "Undefined symbols for
architecture x86_64" (#628, release-rust run 38469217720). These run
obs_rust_cargo_target from libobs/cmake/rust-target.cmake with `cmake -P`.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULE = ROOT / "libobs" / "cmake" / "rust-target.cmake"


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class RustCargoTargetTest(unittest.TestCase):
    def target(self, macos: bool, archs: str) -> str:
        with tempfile.TemporaryDirectory() as d:
            script = Path(d) / "t.cmake"
            script.write_text(
                f'include("{MODULE.as_posix()}")\n'
                f'obs_rust_cargo_target(t {"ON" if macos else "OFF"} "{archs}")\n'
                'message(STATUS "TARGET=[${t}]")\n'
            )
            res = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            out = res.stdout + res.stderr
            return out.split("TARGET=[", 1)[1].split("]", 1)[0]

    def test_macos_x86_64_is_cross_target(self):
        self.assertEqual(self.target(True, "x86_64"), "x86_64-apple-darwin")

    def test_macos_arm64(self):
        self.assertEqual(self.target(True, "arm64"), "aarch64-apple-darwin")

    def test_unset_or_universal_keeps_the_host_default(self):
        self.assertEqual(self.target(True, ""), "")
        self.assertEqual(self.target(True, "arm64;x86_64"), "")

    def test_not_macos_keeps_the_host_default(self):
        self.assertEqual(self.target(False, "x86_64"), "")


if __name__ == "__main__":
    unittest.main()
