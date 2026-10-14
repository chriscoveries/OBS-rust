#!/usr/bin/env -S uv run --script
"""Shared registration lists are split per unit (obs-rust/obs-studio#137).

Every port used to append to the same few lists, so concurrent port PRs
conflicted with each other on every merge. These checks keep the lists
fragment-based or derived.

    uv run python -m unittest ci/test_registration.py
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORACLE = ROOT / "rust" / "obs-c-oracle"


class RegistrationTest(unittest.TestCase):
    def test_oracle_sources_are_globbed(self):
        # build.rs compiles oracle/*.c; a port adds a file, not a line.
        build_rs = (ORACLE / "build.rs").read_text()
        self.assertNotRegex(build_rs, r'\.file\("oracle/[^"]+\.c"\)')

    def test_oracle_modules_live_in_files(self):
        # lib.rs is a sorted list of `pub mod x;` lines; no inline bodies.
        lib = (ORACLE / "src" / "lib.rs").read_text()
        self.assertNotRegex(lib, r"(?m)^pub mod \w+ \{")
        mods = re.findall(r"(?m)^pub mod (\w+);", lib)
        self.assertEqual(mods, sorted(mods))

    def test_cmocka_tests_are_fragments(self):
        cmake = (ROOT / "test" / "cmocka" / "CMakeLists.txt").read_text()
        self.assertIn("tests.d/*.cmake", cmake)

    def test_tier2_object_list_is_derived(self):
        # validate.sh must not hard-code which ported C objects to check.
        validate = (ROOT / "rust" / "tools" / "linux-validate" / "validate.sh").read_text()
        self.assertNotRegex(validate, r"for obj in [a-z-]+\.c\.o ")

    def test_rust_exports_are_fragments(self):
        fragments = sorted((ROOT / "libobs" / "cmake" / "rust-exports.d").glob("*.txt"))
        self.assertTrue(fragments, "libobs/cmake/rust-exports.d/*.txt is missing")
        self.assertFalse((ROOT / "libobs" / "cmake" / "rust-exports.txt").exists())


if __name__ == "__main__":
    unittest.main()
