#!/usr/bin/env -S uv run --script
"""Rust crate names follow docs/rust-port/naming.md (#320).

    uv run python -m unittest ci/test_naming.py
"""

from __future__ import annotations

import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXED = {"libobs-rust", "obs-c-oracle", "obs-studio-rust"}
PORT = re.compile(r"^obs-[a-z0-9]+(-[a-z0-9]+)*$")


def crates() -> dict[str, dict]:
    found = {}
    for manifest in sorted((ROOT / "rust").glob("*/Cargo.toml")):
        found[manifest.parent.name] = tomllib.loads(manifest.read_text())
    return found


class NamingTest(unittest.TestCase):
    def test_crate_names(self):
        for directory, manifest in crates().items():
            name = manifest["package"]["name"]
            with self.subTest(crate=name):
                self.assertEqual(name, directory, "directory name must equal the package name")
                if name in FIXED:
                    continue
                self.assertRegex(name, PORT, "crates are obs-<area> (see naming.md)")
                self.assertFalse(name.endswith("-rust"), "only libobs-rust and obs-studio-rust end in -rust")

    def test_binaries_use_the_product_name(self):
        for manifest in crates().values():
            for binary in manifest.get("bin", []):
                with self.subTest(binary=binary["name"]):
                    self.assertEqual(binary["name"], "obs-studio-rust")


if __name__ == "__main__":
    unittest.main()
