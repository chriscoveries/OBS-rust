#!/usr/bin/env -S uv run --script
"""GCC/Clang warning flags must not reach MSVC (obs-rust/obs-studio#634 item 8).

    uv run python -m unittest ci/test_msvc_flags.py

MSVC rejects `-W...` options it doesn't know (D8021) and fails the Windows
build late, after every other platform passed. ci/msvc_flags.py flags a
`-W` option passed to target_compile_options/add_compile_options/
COMPILE_OPTIONS unless it is inside a compiler-id generator expression, an
if() branch that excludes MSVC, or a file that only builds off Windows.
"""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("msvc_flags", ROOT / "ci" / "msvc_flags.py")
msvc_flags = importlib.util.module_from_spec(spec)
spec.loader.exec_module(msvc_flags)


def flagged(text: str, path: str = "plugins/x/CMakeLists.txt") -> list[str]:
    return [flag for _, flag in msvc_flags.unguarded_flags(path, text)]


class Flagged(unittest.TestCase):
    def test_bare_flag(self):
        self.assertEqual(flagged("target_compile_options(x PRIVATE -Wno-vla)\n"), ["-Wno-vla"])

    def test_multiline_call(self):
        text = "target_compile_options(\n  x\n  PRIVATE\n    -Wno-shadow\n    -Werror=vla\n)\n"
        self.assertEqual(flagged(text), ["-Wno-shadow", "-Werror=vla"])

    def test_add_compile_options_and_property(self):
        text = 'add_compile_options(-Wall)\nset_source_files_properties(a.c PROPERTIES COMPILE_OPTIONS "-Wno-x")\n'
        self.assertEqual(flagged(text), ["-Wall", "-Wno-x"])

    def test_branch_that_includes_msvc(self):
        text = "if(OS_WINDOWS)\n  target_compile_options(x PRIVATE -Wno-x)\nendif()\n"
        self.assertEqual(flagged(text), ["-Wno-x"])

    def test_else_of_a_non_msvc_branch(self):
        text = "if(NOT MSVC)\n  set(a b)\nelse()\n  target_compile_options(x PRIVATE -Wno-x)\nendif()\n"
        self.assertEqual(flagged(text), ["-Wno-x"])


class Allowed(unittest.TestCase):
    def test_generator_expression(self):
        self.assertEqual(flagged("target_compile_options(x PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Wno-vla>)\n"), [])

    def test_msvc_options(self):
        self.assertEqual(flagged("target_compile_options(x PRIVATE /wd4996 /W4)\n"), [])

    def test_non_msvc_branches(self):
        for cond in [
            "NOT MSVC",
            'CMAKE_C_COMPILER_ID STREQUAL "GNU"',
            "OS_LINUX",
            "OS_MACOS",
            "APPLE",
            "OS_FREEBSD OR OS_OPENBSD",
        ]:
            with self.subTest(cond=cond):
                text = f"if({cond})\n  target_compile_options(x PRIVATE -Wno-x)\nendif()\n"
                self.assertEqual(flagged(text), [])

    def test_multiline_elseif(self):
        text = (
            'if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")\n  target_compile_options(x PRIVATE /wd4996)\n'
            'elseif(\n  CMAKE_C_COMPILER_ID STREQUAL "GNU"\n  OR CMAKE_C_COMPILER_ID STREQUAL "Clang"\n)\n'
            "  target_compile_options(x PRIVATE -Wno-x)\nendif()\n"
        )
        self.assertEqual(flagged(text), [])

    def test_else_of_windows_branch(self):
        text = "if(OS_WINDOWS OR OS_MACOS)\n  set(a b)\nelse()\n  target_compile_options(x PRIVATE -Wno-x)\nendif()\n"
        self.assertEqual(flagged(text), [])

    def test_platform_only_files(self):
        text = "target_compile_options(x PRIVATE -Wno-x)\n"
        for path in [
            "cmake/macos/compilerconfig.cmake",
            "cmake/linux/compilerconfig.cmake",
            "plugins/mac-capture/CMakeLists.txt",
            "plugins/linux-v4l2/CMakeLists.txt",
            "plugins/sndio/CMakeLists.txt",
        ]:
            with self.subTest(path=path):
                self.assertEqual(flagged(text, path), [])

    def test_comments_and_other_commands(self):
        text = "# target_compile_options(x PRIVATE -Wno-x)\nmessage(STATUS \"-Wall\")\nset(_flags -Wall)\n"
        self.assertEqual(flagged(text), [])


class Repository(unittest.TestCase):
    def test_no_unguarded_flags(self):
        self.assertEqual(msvc_flags.scan(ROOT), [])


if __name__ == "__main__":
    unittest.main()
