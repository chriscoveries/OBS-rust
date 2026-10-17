#!/usr/bin/env -S uv run --script
"""Clippy and Dylint for the PR gate's cross targets, checked from Linux (#472, #955).

    uv run python -m unittest ci/test_cross_lane.py

soldr cross-compiles from a Linux host, so one Linux lane lints the PR
gate's Windows x64 and macOS arm64 targets (zackees/ci.yml, the soldr
pilot's phase 2): the `cross` local-gate lane attests
rust/<target>/{clippy,dylint}, and CI's rust-cross-lint job runs the same
lane on every PR push that isn't attested. Windows arm64, macOS x64 and
Linux arm64 are linted only in the release gate (`cross-release`).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ci"))

import local_gate  # noqa: E402

TARGETS = {"x86_64-pc-windows-msvc", "aarch64-apple-darwin"}


class CrossLaneTest(unittest.TestCase):
    def test_lane_covers_every_target(self):
        self.assertIn("cross", local_gate.LANES)
        self.assertEqual(set(getattr(local_gate, "CROSS_TARGETS", ())), TARGETS)

    def test_gates_are_declared(self):
        text = (ROOT / "ci-attestations.yml").read_text()
        for target in sorted(TARGETS):
            for check in ("clippy", "dylint"):
                with self.subTest(target=target, check=check):
                    self.assertIn(f"rust/{target}/{check}: {{lane: cross}}", text)
        self.assertIn("build-project.yaml:rust-cross-lint:", text)

    def test_ci_runs_the_lane(self):
        text = (ROOT / ".github" / "workflows" / "build-project.yaml").read_text()
        self.assertIn("  rust-cross-lint:", text)
        self.assertIn("ci/local_gate.py --lane cross", text)


if __name__ == "__main__":
    unittest.main()
