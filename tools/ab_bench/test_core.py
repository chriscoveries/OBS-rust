# SPDX-License-Identifier: GPL-2.0-or-later
"""Timing-free tests for shared A/B benchmark reporting helpers."""

import unittest
from pathlib import Path

from core import (
    bootstrap_median_interval,
    describe,
    interleaved_schedule,
    ns_per_frame,
    parse_proc_stat,
    parse_top_cpu,
    proc_stat_busy,
    spreads_overlap,
    summarize_rounds,
)

FIXTURES = Path(__file__).with_name("testdata")


class StatisticsTests(unittest.TestCase):
    def test_zero_duration_batch_is_rejected_clearly(self):
        with self.assertRaisesRegex(ValueError, r"batch duration must be > 0 ns"):
            ns_per_frame(0, 1)

    def test_summary_rejects_zero_duration_sample_clearly(self):
        rows = [
            {"case": "BGRA same", "before_ns": 0, "after_ns": 10, "frames": 1}
        ]
        with self.assertRaisesRegex(ValueError, r"batch duration must be > 0 ns"):
            summarize_rounds(rows)

    def test_ns_per_frame_uses_full_batch_duration(self):
        self.assertEqual(ns_per_frame(10001, 3), 10001 / 3)

    def test_median_minimum_and_iqr(self):
        stats = describe([1, 2, 3, 4, 5, 6, 7])
        self.assertEqual(stats["median"], 4)
        self.assertEqual(stats["min"], 1)
        self.assertEqual(stats["q1"], 2.5)
        self.assertEqual(stats["q3"], 5.5)
        self.assertEqual(stats["iqr"], 3)

    def test_bootstrap_ratio_interval_is_stable(self):
        first = bootstrap_median_interval([1.9, 2.0, 2.1, 2.0], seed=17)
        second = bootstrap_median_interval([1.9, 2.0, 2.1, 2.0], seed=17)
        self.assertEqual(first, second)
        self.assertLessEqual(first[0], 2.0)
        self.assertGreaterEqual(first[1], 2.0)

    def test_spread_overlap_marks_noise(self):
        self.assertTrue(
            spreads_overlap(describe([10, 11, 12, 13]), describe([11, 12, 13, 14]))
        )
        self.assertFalse(
            spreads_overlap(describe([10, 11, 12, 13]), describe([20, 21, 22, 23]))
        )

    def test_interleaves_versions(self):
        self.assertEqual(
            interleaved_schedule(7), ["AB", "BA", "AB", "BA", "AB", "BA", "AB"]
        )

    def test_summary_sets_inconclusive_when_iqr_ranges_overlap(self):
        rows = []
        for index, (before, after) in enumerate(
            (
                (100, 101),
                (101, 102),
                (102, 100),
                (103, 104),
                (104, 103),
                (105, 106),
                (106, 105),
            )
        ):
            rows.append(
                {
                    "case": "NV12 same",
                    "before_ns": before,
                    "after_ns": after,
                    "frames": 1,
                    "round": index,
                }
            )
        result = summarize_rounds(rows)[0]
        self.assertTrue(result["noise_inconclusive"])

    def test_summarize_reports_ratio_interval_and_noise(self):
        rows = []
        for index, (before, after) in enumerate(
            (
                (200, 100),
                (210, 110),
                (190, 90),
                (205, 102),
                (195, 98),
                (201, 99),
                (198, 101),
            )
        ):
            rows.append(
                {
                    "case": "BGRA same",
                    "before_ns": before,
                    "after_ns": after,
                    "frames": 1,
                }
            )
        result = summarize_rounds(rows)[0]
        self.assertEqual(result["rounds"], 7)
        self.assertGreater(result["speedup"], 1.0)
        self.assertEqual(len(result["speedup_95ci"]), 2)
        self.assertFalse(result["noise_inconclusive"])


class LoadParserTests(unittest.TestCase):
    def test_parse_recorded_macos_top_samples(self):
        text = (FIXTURES / "top-sample.txt").read_text(encoding="utf-8")
        self.assertEqual(parse_top_cpu(text), [15.0, 40.0, 30.0])

    def test_parse_recorded_linux_proc_stat_delta(self):
        before = parse_proc_stat(
            (FIXTURES / "proc-stat-before.txt").read_text(encoding="ascii")
        )
        after = parse_proc_stat(
            (FIXTURES / "proc-stat-after.txt").read_text(encoding="ascii")
        )
        self.assertEqual(before, (980, 810))
        self.assertEqual(after, (1130, 910))
        self.assertAlmostEqual(proc_stat_busy(before, after), 33.3333333333)

    def test_reject_missing_load_lines(self):
        with self.assertRaises(ValueError):
            parse_top_cpu("no CPU data")
        with self.assertRaises(ValueError):
            parse_proc_stat("intr 1 2 3")


if __name__ == "__main__":
    unittest.main()
