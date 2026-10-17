#!/usr/bin/env -S uv run --script
"""The release's AppImage is built with ENABLE_RUST_LIBOBS whatever started it.

release-rust.yaml calls appimage.yaml. Inside a called workflow
`github.event_name` is the caller's event, so on a manually dispatched release
(run 38497010624) `github.event_name != 'workflow_dispatch' || inputs.rust_libobs`
read a workflow_dispatch-only input that a workflow_call never sets: the
AppImage was built without Rust libobs and its --version check failed.

    uv run python -m unittest ci/test_appimage_call_inputs.py
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

WORKFLOW = Path(__file__).resolve().parent.parent / ".github" / "workflows" / "appimage.yaml"


class AppImageCallInputsTest(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text()

    def test_workflow_call_declares_rust_libobs_defaulting_on(self):
        call = self.text.split("workflow_call:", 1)[1].split("\n  workflow_dispatch:", 1)[0]
        m = re.search(r"\n      rust_libobs:\n(?:        .*(?:\n|$))+", call)
        self.assertIsNotNone(m, "workflow_call has no rust_libobs input")
        self.assertIn("default: true", m.group(0))

    def test_workflow_call_declares_rust_libobs_only_once(self):
        call = self.text.split("workflow_call:", 1)[1].split("\n  workflow_dispatch:", 1)[0]
        declarations = re.findall(r"^      rust_libobs:$", call, re.M)
        self.assertEqual(len(declarations), 1, "duplicate workflow_call input keys prevent any job from starting")

    def test_rust_libobs_does_not_depend_on_the_event_name(self):
        line = next(l for l in self.text.splitlines() if "RUST_LIBOBS:" in l)
        self.assertNotIn("github.event_name", line, line)


if __name__ == "__main__":
    unittest.main()
