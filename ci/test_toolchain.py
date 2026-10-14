#!/usr/bin/env -S uv run --script
"""The Rust toolchain is pinned and every crate declares the MSRV
(obs-rust/obs-studio#136).

    uv run python -m unittest ci/test_toolchain.py
"""

from __future__ import annotations

import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


# The one place the expected Rust version lives. It is the latest toolchain the
# soldr version CI uses (setup-soldr `version:` in build-project.yaml) supports
# natively (that release's own rust-toolchain.toml in zackees/soldr). Change it
# only together with a soldr bump that supports the new version.
SOLDR_SUPPORTED_RUST = "1.98.1"

# Other places a Rust version could be pinned and drift.
PIN_SCAN = ("rust/tools/**/Dockerfile*", "rust/tools/**/*.sh", ".github/workflows/*.y*ml", "docs/**/*.md", "AGENTS.md")
PIN_RE = re.compile(r"(?:toolchain|channel|rustc|rust)[\s:=\"'@-]+v?(\d+\.\d+\.\d+)\b", re.I)


def version(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split("."))


class ToolchainTest(unittest.TestCase):
    def setUp(self):
        self.toolchain = tomllib.loads((ROOT / "rust-toolchain.toml").read_text())["toolchain"]
        self.workspace = tomllib.loads((ROOT / "Cargo.toml").read_text())

    def test_channel_is_an_exact_version(self):
        # "stable" floats: a new release can change clippy -D warnings or
        # codegen for everyone at once.
        self.assertRegex(self.toolchain["channel"], r"^\d+\.\d+\.\d+$")

    def test_channel_is_the_soldr_supported_version(self):
        self.assertEqual(self.toolchain["channel"], SOLDR_SUPPORTED_RUST, "rust-toolchain.toml must pin the latest Rust the CI soldr supports (SOLDR_SUPPORTED_RUST in ci/test_toolchain.py); bump only after a soldr bump")

    def test_msrv_matches_pin(self):
        msrv = self.workspace["workspace"]["package"]["rust-version"]
        self.assertEqual(msrv, ".".join(SOLDR_SUPPORTED_RUST.split(".")[:2]))

    def test_no_other_pin_disagrees(self):
        bad = []
        for pattern in PIN_SCAN:
            for path in sorted(ROOT.glob(pattern)):
                for m in PIN_RE.finditer(path.read_text(errors="ignore")):
                    if m.group(1) != SOLDR_SUPPORTED_RUST:
                        bad.append(f"{path.relative_to(ROOT)}: {m.group(0).strip()}")
        self.assertEqual(bad, [])

    def test_workspace_declares_msrv(self):
        msrv = self.workspace["workspace"]["package"].get("rust-version")
        self.assertIsNotNone(msrv, "[workspace.package] rust-version is missing")
        self.assertRegex(msrv, r"^\d+\.\d+(\.\d+)?$")
        self.assertLessEqual(version(msrv), version(self.toolchain["channel"]))

    def test_crates_inherit_msrv(self):
        for manifest in sorted(ROOT.glob("rust/*/Cargo.toml")):
            package = tomllib.loads(manifest.read_text())["package"]
            with self.subTest(crate=manifest.parent.name):
                self.assertEqual(package.get("rust-version"), {"workspace": True})


if __name__ == "__main__":
    unittest.main()
