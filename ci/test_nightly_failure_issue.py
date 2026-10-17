#!/usr/bin/env -S uv run --script
"""The nightly full run opens an issue when it fails (#992).

    uv run python -m unittest ci/test_nightly_failure_issue.py

PRs and the merge queue run only the fast path, so the scheduled run on master
is the safety net for anything that slips through. It is only useful if a
failure is noticed, so scheduled.yaml files (or updates) an issue on failure.
"""

from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEDULED = ROOT / ".github" / "workflows" / "scheduled.yaml"


class NightlyFailureIssueTest(unittest.TestCase):
    def setUp(self):
        text = SCHEDULED.read_text(encoding="utf-8")
        start = text.index("  report-nightly-failure:")
        end = text.index("\n  analyze-project:", start)
        self.job = text[start:end]

    def test_runs_only_after_a_failed_scheduled_build(self):
        self.assertIn("needs: build-project", self.job)
        self.assertIn("if: failure() && github.event_name == 'schedule'", self.job)

    def test_can_write_issues(self):
        self.assertIn("issues: write", self.job)

    def test_reuses_an_open_issue_instead_of_piling_up(self):
        self.assertIn("gh issue list --state open", self.job)
        self.assertIn("gh issue comment", self.job)
        self.assertIn("gh issue create", self.job)


if __name__ == "__main__":
    unittest.main()
