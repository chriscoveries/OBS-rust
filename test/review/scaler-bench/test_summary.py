#!/usr/bin/env -S uv run --script
import unittest
from summarize import summarize


def records(quick=False):
    out = [{"kind": "environment", "swscale_version": 1, "avutil_version": 1}]
    for fmt in ("BGRA", "I420", "NV12", "P010"):
        for sw, sh, dw, dh in ((1920, 1080, 1920, 1080), (3840, 2160, 1920, 1080), (1920, 1080, 3840, 2160)):
            for pair in range(8):
                out.append(dict(kind="pair", format=fmt, src_w=sw, src_h=sh, dst_w=dw, dst_h=dh,
                                pair=pair, frames=10, order="DC" if pair % 2 else "CD", copy_calls=10,
                                direct_calls=10, copy_ns=200, direct_ns=100, equal=True, quick=quick))
    return out


class SummaryTests(unittest.TestCase):
    def test_paired_result(self):
        report = summarize(records())
        self.assertEqual(len(report["cases"]), 12)
        self.assertEqual(report["cases"][0]["median_time_saved_percent"], 50)

    def test_truncated(self):
        with self.assertRaises(ValueError): summarize(records()[:-1])

    def test_duplicate(self):
        rows = records(); rows[2] = rows[1].copy()
        with self.assertRaises(ValueError): summarize(rows)

    def test_wrong_path(self):
        rows = records(); rows[1]["direct_calls"] = 0
        with self.assertRaises(ValueError): summarize(rows)

    def test_nonfinite_timing(self):
        rows = records(); rows[1]["direct_ns"] = float("nan")
        with self.assertRaises(ValueError): summarize(rows)

    def test_wrong_pixels(self):
        rows = records(); rows[1]["equal"] = False
        with self.assertRaises(ValueError): summarize(rows)

    def test_smoke_has_no_performance_result(self):
        report = summarize(records(True))
        self.assertEqual(report["mode"], "smoke_only")
        self.assertNotIn("median_time_saved_percent", report["cases"][0])

    def test_mixed_modes(self):
        rows = records(); rows[1]["quick"] = True
        with self.assertRaises(ValueError): summarize(rows)


if __name__ == "__main__":
    unittest.main()
