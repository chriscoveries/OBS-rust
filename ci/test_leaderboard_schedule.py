#!/usr/bin/env -S uv run --script
"""The contributor leaderboard refreshes every 4 hours.

    uv run python -m unittest ci/test_leaderboard_schedule.py
"""

from __future__ import annotations

import importlib.util
import re
import unittest
from pathlib import Path

WORKFLOW = Path(__file__).resolve().parent.parent / ".github" / "workflows" / "leaderboard.yml"
GENERATE = Path(__file__).resolve().parent.parent / "tools" / "leaderboard" / "generate.py"


def load_generate():
    spec = importlib.util.spec_from_file_location("leaderboard_generate", GENERATE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class CompletenessCheckTest(unittest.TestCase):
    """Run 38335083387 failed with "INCOMPLETE: fetched 219 merged PRs, GitHub
    reports 218": a PR merged while the generator was fetching, so the list
    was newer than the count. Only a list shorter than the count is a real
    gap; the job must not fail (and the badge go red) for a fresher list."""

    def setUp(self):
        self.gen = load_generate()

    def test_fresher_list_than_count_is_complete(self):
        self.assertTrue(self.gen.merged_list_complete(219, 218))

    def test_equal_is_complete(self):
        self.assertTrue(self.gen.merged_list_complete(218, 218))

    def test_short_list_is_incomplete(self):
        self.assertFalse(self.gen.merged_list_complete(217, 218))

    def test_count_is_read_before_the_list(self):
        src = GENERATE.read_text()
        self.assertLess(src.index('"search/issues"'), src.index('"pr", "list"'))


class LeaderboardScheduleTest(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text()

    def test_runs_every_four_hours(self):
        crons = re.findall(r'cron:\s*"([^"]+)"', self.text)
        self.assertEqual(len(crons), 1, crons)
        minute, hour, *rest = crons[0].split()
        self.assertEqual(hour, "*/4", crons[0])
        self.assertEqual(rest, ["*", "*", "*"], crons[0])

    def test_no_time_of_day_gate(self):
        # A scheduled run must always regenerate; nothing may skip it by hour.
        self.assertNotIn("America/Los_Angeles", self.text)
        self.assertNotIn("steps.gate", self.text)

    def test_readme_badge_points_at_workflow(self):
        readme = (WORKFLOW.parent.parent.parent / "README.md").read_text()
        self.assertIn("actions/workflows/leaderboard.yml/badge.svg?branch=master", readme)

    def test_still_dispatchable(self):
        self.assertIn("workflow_dispatch:", self.text)


if __name__ == "__main__":
    unittest.main()
