#!/usr/bin/env -S uv run --script
"""Tier 2 Linux shards are balanced by recorded test duration (#1048).

    uv run python -m unittest ci/test_tier2_shard_plan.py

`ctest -I <i>,,3` striped tests by number, so one shard ran tests for
153 s and another for 63 s (master run 38874213546). shard-plan.sh assigns
tests longest-first to the lightest shard using ctest's own cost data
(CTestCostData.txt, carried in the Tier 2 ccache since #931) and writes one
exact-name regex per shard for `ctest -R`.
"""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLAN = ROOT / "rust" / "tools" / "linux-validate" / "shard-plan.sh"
VALIDATE = ROOT / "rust" / "tools" / "linux-validate" / "validate.sh"

# Shaped like the real data: a few long real-time tests, many short ones.
COSTS = {
    "test_deinterlace_2x_field_order": 82.0,
    "test_async_delay_audio": 59.0,
    "test_startup_cycle_stress": 48.0,
    "test_portal_shortcuts": 43.0,
    "test_vlc_audio_pacing": 35.0,
    "test_vlc_playlist_gap": 26.0,
    **{f"test_short_{i:02d}": 0.5 + (i % 7) * 0.3 for i in range(40)},
}


def run_plan(costs: dict[str, float], names: list[str], shards: int) -> dict[int, list[str]]:
    d = Path(tempfile.mkdtemp())
    cost = d / "CTestCostData.txt"
    cost.write_text("".join(f"{n} 3 {c}\n" for n, c in costs.items()) + ("---\ntest_flaky\n" if costs else ""))
    listing = d / "names.txt"
    listing.write_text("\n".join(names) + "\n")
    out = d / "out"
    r = subprocess.run(["bash", str(PLAN), str(cost), str(listing), str(shards), str(out)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    plan = {}
    for i in range(1, shards + 1):
        regex = (out / f"{i}-of-{shards}.regex").read_text().strip()
        m = re.fullmatch(r"\^\((.*)\)\$", regex)
        assert m, regex
        plan[i] = m.group(1).split("|") if m.group(1) else []
    return plan


class ShardPlanTest(unittest.TestCase):
    def test_every_test_in_exactly_one_shard(self):
        names = list(COSTS)
        plan = run_plan(COSTS, names, 3)
        assigned = [n for tests in plan.values() for n in tests]
        self.assertEqual(sorted(assigned), sorted(names))

    def test_loads_are_balanced(self):
        plan = run_plan(COSTS, list(COSTS), 3)
        loads = [sum(COSTS[n] for n in tests) for tests in plan.values()]
        total = sum(COSTS.values())
        # Longest-first greedy keeps the heaviest shard near total/3 (the
        # 82 s test alone bounds it from below).
        self.assertLessEqual(max(loads), max(total / 3 * 1.15, 82.0 + 5))

    def test_longest_tests_go_to_different_shards(self):
        plan = run_plan(COSTS, list(COSTS), 3)
        where = {n: i for i, tests in plan.items() for n in tests}
        top3 = ["test_deinterlace_2x_field_order", "test_async_delay_audio", "test_startup_cycle_stress"]
        self.assertEqual(len({where[n] for n in top3}), 3)

    def test_tests_without_cost_data_are_still_planned(self):
        names = list(COSTS) + ["test_brand_new"]
        plan = run_plan(COSTS, names, 3)
        self.assertIn("test_brand_new", [n for t in plan.values() for n in t])

    def test_no_cost_data_still_splits_every_test(self):
        """First run, or no saved timings: the cost file is empty. awk's
        FNR == NR then misread the names as costs and every shard came out
        empty."""
        plan = run_plan({}, ["test_a", "test_b", "test_c", "test_d"], 2)
        self.assertEqual(sorted(n for t in plan.values() for n in t), ["test_a", "test_b", "test_c", "test_d"])
        self.assertEqual(sorted(len(t) for t in plan.values()), [2, 2])

    def test_regex_matches_exact_names_only(self):
        plan = run_plan({"test_a": 1.0, "test_ab": 1.0}, ["test_a", "test_ab"], 1)
        self.assertEqual(sorted(plan[1]), ["test_a", "test_ab"])


class ValidateUsesPlanTest(unittest.TestCase):
    def test_build_stage_writes_and_test_stage_uses_the_plan(self):
        text = VALIDATE.read_text(encoding="utf-8")
        self.assertIn("shard-plan.sh", text)
        self.assertIn("tier2-shards", text)
        self.assertRegex(text, r'shard_args=\(-R "\$\(cat "\$plan"\)"\)')


if __name__ == "__main__":
    unittest.main()
