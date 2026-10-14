#!/usr/bin/env -S uv run --script
"""Feature ideas go to Discussions, not the issue tracker (#332).

    uv run python -m unittest ci/test_issue_templates.py
"""

from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IDEAS = "https://github.com/obs-rust/obs-studio/discussions/categories/ideas"


class IssueTemplateTest(unittest.TestCase):
    def test_ideas_link_points_at_discussions(self):
        config = (ROOT / ".github/ISSUE_TEMPLATE/config.yml").read_text()
        self.assertIn(f"url: {IDEAS}", config)

    def test_coordination_guide_routes_ideas(self):
        guide = (ROOT / "docs/rust-port/coordination.md").read_text()
        self.assertIn(IDEAS, guide)


if __name__ == "__main__":
    unittest.main()
