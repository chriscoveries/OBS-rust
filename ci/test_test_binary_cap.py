#!/usr/bin/env -S uv run --script
"""Unit tests for ci/test_binary_cap.py (#968)."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_binary_cap as cap  # noqa: E402


def write(path: Path, text: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


class CountTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        write(self.root / "Cargo.toml", '[workspace]\nmembers = ["rust/*"]\nexclude = ["rust/tools"]\n')
        write(self.root / "rust/tools/Cargo.toml", '[package]\nname = "tools"\n')
        write(self.root / "rust/tools/tests/a.rs")

    def tearDown(self):
        self.tmp.cleanup()

    def crate(self, name: str, manifest: str = "") -> Path:
        crate = self.root / "rust" / name
        write(crate / "Cargo.toml", f'[package]\nname = "{name}"\n{manifest}')
        return crate

    def test_counts_files_category_dirs_and_explicit_targets(self):
        crate = self.crate("a", '\n[[test]]\nname = "extra"\npath = "other/extra.rs"\n')
        write(crate / "tests/one.rs")
        write(crate / "tests/two.rs")
        write(crate / "tests/cat/main.rs")
        write(crate / "tests/cat/one_mod.rs")
        write(crate / "tests/common/mod.rs")
        self.assertEqual(cap.workspace_counts(self.root), {"a": 4})

    def test_explicit_target_for_auto_file_counts_once(self):
        crate = self.crate("a", '\n[[test]]\nname = "one"\npath = "tests/one.rs"\nharness = false\n')
        write(crate / "tests/one.rs")
        self.assertEqual(cap.workspace_counts(self.root), {"a": 1})

    def test_autotests_false_counts_only_explicit(self):
        crate = self.crate("a", 'autotests = false\n\n[[test]]\nname = "x"\npath = "tests/x.rs"\n')
        write(crate / "tests/x.rs")
        write(crate / "tests/y.rs")
        self.assertEqual(cap.workspace_counts(self.root), {"a": 1})

    def test_excluded_member_is_skipped(self):
        self.assertNotIn("tools", cap.workspace_counts(self.root))

    def test_violations(self):
        self.assertEqual(cap.violations({"a": 8, "b": 1}, per_crate=8, total=9), [])
        errors = cap.violations({"a": 9, "b": 1}, per_crate=8, total=9)
        self.assertEqual(len(errors), 2)
        self.assertIn("a: 9", errors[0])
        self.assertIn("workspace: 10", errors[1])


if __name__ == "__main__":
    unittest.main()
