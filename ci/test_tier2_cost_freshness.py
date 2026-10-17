#!/usr/bin/env -S uv run --script
"""Tier 2 Linux shards hand their test timings back (#930).

    uv run python -m unittest ci/test_tier2_cost_freshness.py

Since the build/test split (#975) the build job plans the shards from
ctest-cost-<mode>.txt in the Tier 2 ccache, but only shards run ctest and
they kept no cache, so the cost data froze at its last pre-split value and
new or slower tests were planned at the 1 s default. Each shard now uploads
its CTestCostData.txt, the done job merges them per mode (merge-costs.sh)
and caches the result on master, and the build job restores it.
"""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MERGE = ROOT / "rust" / "tools" / "linux-validate" / "merge-costs.sh"
VALIDATE = ROOT / "rust" / "tools" / "linux-validate" / "validate.sh"
WORKFLOW = ROOT / ".github" / "workflows" / "build-project.yaml"


def job(name: str) -> str:
    text = WORKFLOW.read_text(encoding="utf-8")
    m = re.search(rf"^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9-]+:\n|\Z)", text, re.S | re.M)
    assert m, f"no job {name}"
    return m.group(1)


def merge(*files: str) -> str:
    d = Path(tempfile.mkdtemp())
    paths = []
    for i, body in enumerate(files):
        p = d / f"{i}.txt"
        p.write_text(body)
        paths.append(str(p))
    r = subprocess.run(["bash", str(MERGE), *paths], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout


class MergeCostsTest(unittest.TestCase):
    def test_takes_each_test_from_the_shard_that_ran_it(self):
        # Each shard starts from the same old data (a 3 10, b 3 20, c 3 30)
        # and updates only the tests it ran.
        s1 = "a 4 12\nb 3 20\nc 3 30\n---\n"
        s2 = "a 3 10\nb 4 25\nc 3 30\n---\n"
        s3 = "a 3 10\nb 3 20\nc 4 33\nnew 1 7\n---\nc\n"
        out = merge(s1, s2, s3)
        lines, _, failed = out.partition("---\n")
        self.assertEqual(sorted(lines.splitlines()), ["a 4 12", "b 4 25", "c 4 33", "new 1 7"])
        self.assertEqual(failed.split(), ["c"])

    def test_empty_inputs_give_empty_data(self):
        self.assertEqual(merge("", ""), "---\n")


class WorkflowTest(unittest.TestCase):
    def test_validate_reads_a_cost_dir(self):
        text = VALIDATE.read_text(encoding="utf-8")
        self.assertIn("OBS_TIER2_COST_DIR", text)

    def test_test_job_uploads_its_costs(self):
        j = job("rust-linux-tier2")
        self.assertIn("OBS_TIER2_COST_DIR", j)
        self.assertIn("name: tier2-cost\n", j)
        self.assertIn("path: .tier2-cost/ctest-cost-*.txt", j)

    def test_shards_start_from_the_merged_costs(self):
        # ctest starts the longest tests first only with cost data; shards
        # ran with an empty dir, so the 59 s and 35 s tests started last and
        # shard 2 took 168 s for 228 s of work at -j4 (run 38892442457).
        j = job("rust-linux-tier2")
        restore = j.index("restore-keys: tier2-ctest-cost-")
        self.assertLess(restore, j.index("- name: Test Tier 2 ("))
        self.assertIn("path: .tier2-cost", j)
        self.assertIn("OBS_TIER2_COST_DIR: ${{ github.workspace }}/.tier2-cost", j)

    def test_done_job_merges_and_caches_on_master(self):
        j = job("rust-linux-tier2-done")
        self.assertIn("pattern: tier2-cost*", j)
        self.assertIn("merge-costs.sh", j)
        self.assertIn("actions/cache/save@", j)
        self.assertIn("key: tier2-ctest-cost-${{ github.sha }}", j)

    def test_build_job_restores_the_merged_costs(self):
        j = job("rust-linux-tier2-build")
        self.assertIn("restore-keys: tier2-ctest-cost-", j)
        self.assertIn("OBS_TIER2_COST_DIR", j)


if __name__ == "__main__":
    unittest.main()
