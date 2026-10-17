#!/usr/bin/env -S uv run --script
"""The full Tier 4 suite runs against the real app built with Rust libobs (#140).

    uv run python -m unittest ci/test_tier4_rust_app.py

The Ubuntu ENABLE_RUST_LIBOBS=ON build (#167) only ran Tier 4's GPU check,
so the Rust code never answered obs-websocket requests inside OBS itself.
The ON leg now also runs the whole suite (auth handshake, every request
type, scene collections, inputs, StartRecord/StopRecord) headless against
the installed build. The Ubuntu build runs only in full and release-gate
runs, never on a PR, so this stays off the critical path.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "build-project.yaml"


def step(text: str, name: str) -> str:
    m = re.search(rf"      - name: {re.escape(name)}[^\n]*\n(?:        [^\n]*\n|\n)+", text)
    assert m, f"no step {name}"
    return m.group(0)


class Tier4RustAppTest(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text(encoding="utf-8")

    def test_full_suite_runs_on_the_on_build(self):
        s = step(self.text, "Tier 4 Suite (Rust libobs app)")
        self.assertIn("matrix.rust == 'ON'", s)
        self.assertIn("timeout-minutes:", s)
        run = s[s.index("run_tier4.py"):]
        self.assertNotIn("--only", run.splitlines()[0])
        self.assertIn("--obs /usr/bin/obs-studio-rust-gui", s)

    def test_runs_after_the_install(self):
        self.assertLess(self.text.index("Tier 4 GPU Acceleration Check"), self.text.index("Tier 4 Suite (Rust libobs app)"))


if __name__ == "__main__":
    unittest.main()
