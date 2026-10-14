#!/usr/bin/env -S uv run --script
"""The x86/x64 helper sub-builds on Windows run one at a time.

graphics-hook, get-graphics-offsets, inject-helper and
obs-virtualcam-module each run `cmake --build build_x86 -t <target>` as a
POST_BUILD step. MSBuild ran them in parallel, and two of them regenerated
build_x86's ZERO_CHECK at once: "MSB3374 ... unsuccessfulbuild ... being used
by another process" failed the Windows x64 build on PR #654.
cmake/windows/x86-build-serial.cmake chains them. This configures a tiny
project with that module and checks the chain.

    uv run python -m unittest ci/test_x86_build_serial.py
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULE = ROOT / "cmake" / "windows" / "x86-build-serial.cmake"


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class X86BuildSerialTest(unittest.TestCase):
    def test_registered_targets_build_one_after_another(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "src"
            src.mkdir()
            (src / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\n"
                "project(serial NONE)\n"
                f'include("{MODULE.as_posix()}")\n'
                "foreach(t IN ITEMS hook offsets inject vcam)\n"
                "  add_custom_target(${t})\n"
                "  obs_serialize_x86_build(${t})\n"
                "endforeach()\n"
                "add_custom_target(unrelated)\n"
                'cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _report)\n'
                "function(_report)\n"
                "  foreach(t IN ITEMS hook offsets inject vcam unrelated)\n"
                "    get_target_property(deps ${t} MANUALLY_ADDED_DEPENDENCIES)\n"
                '    message(STATUS "DEPS ${t}=${deps}")\n'
                "  endforeach()\n"
                "endfunction()\n"
            )
            res = subprocess.run(
                ["cmake", "-S", str(src), "-B", str(Path(d) / "build")], capture_output=True, text=True
            )
            out = res.stdout + res.stderr
            self.assertEqual(res.returncode, 0, out)
            self.assertIn("DEPS hook=deps-NOTFOUND", out)
            self.assertIn("DEPS offsets=hook", out)
            self.assertIn("DEPS inject=offsets", out)
            self.assertIn("DEPS vcam=inject", out)
            self.assertIn("DEPS unrelated=deps-NOTFOUND", out)


if __name__ == "__main__":
    unittest.main()
