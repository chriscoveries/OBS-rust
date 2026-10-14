#!/usr/bin/env -S uv run --script
"""The leaderboard shows a green up-arrow with +N for merged PRs since
12:00 AM America/Los_Angeles.

    uv run python -m unittest ci/test_leaderboard_daily_delta.py
"""

from __future__ import annotations

import unittest
from datetime import datetime, timezone

from test_leaderboard_schedule import load_generate

G = load_generate()


def utc(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


class PacificMidnightTest(unittest.TestCase):
    def test_pdt_midnight_is_0700_utc(self):
        # 2026-07-15 is PDT (UTC-7).
        self.assertEqual(G.pacific_midnight(utc("2026-07-15T20:00:00Z")), utc("2026-07-15T07:00:00Z"))

    def test_pst_midnight_is_0800_utc(self):
        # 2026-01-15 is PST (UTC-8).
        self.assertEqual(G.pacific_midnight(utc("2026-01-15T20:00:00Z")), utc("2026-01-15T08:00:00Z"))

    def test_just_before_utc_midnight_is_still_same_pacific_day(self):
        self.assertEqual(G.pacific_midnight(utc("2026-07-16T06:59:59Z")), utc("2026-07-15T07:00:00Z"))

    def test_just_after_midnight_rolls_over(self):
        self.assertEqual(G.pacific_midnight(utc("2026-07-16T07:00:01Z")), utc("2026-07-16T07:00:00Z"))

    def test_dst_transition_day_uses_new_offset_at_midnight(self):
        # 2026-03-08: clocks jump at 02:00; midnight that day is still PST.
        self.assertEqual(G.pacific_midnight(utc("2026-03-08T20:00:00Z")), utc("2026-03-08T08:00:00Z"))


class CountSinceTest(unittest.TestCase):
    CUT = utc("2026-07-15T07:00:00Z")

    def test_counts_only_at_or_after_cutoff(self):
        dates = ["2026-07-15T06:59:59Z", "2026-07-15T07:00:00Z", "2026-07-15T18:00:00Z", "2026-07-10T00:00:00Z"]
        self.assertEqual(G.count_since(dates, self.CUT), 2)

    def test_none(self):
        self.assertEqual(G.count_since([], self.CUT), 0)

    def test_per_contributor_counts(self):
        prs = {
            "alice": ["2026-07-15T08:00:00Z", "2026-07-15T09:00:00Z", "2026-07-01T00:00:00Z"],
            "bob": ["2026-07-14T23:00:00Z"],
        }
        got = {k: G.count_since(v, self.CUT) for k, v in prs.items()}
        self.assertEqual(got, {"alice": 2, "bob": 0})


class BadgeTest(unittest.TestCase):
    def test_positive_has_up_arrow_and_count(self):
        h = G.delta_badge(3)
        self.assertIn("delta-up", h)
        self.assertIn("+3", h)

    def test_zero_has_no_up_arrow(self):
        h = G.delta_badge(0)
        self.assertNotIn("delta-up", h)
        self.assertNotIn("+0", h)

    def test_caption_names_pacific(self):
        self.assertIn("12:00 AM Pacific", G.SINCE_CAPTION)


if __name__ == "__main__":
    unittest.main()
