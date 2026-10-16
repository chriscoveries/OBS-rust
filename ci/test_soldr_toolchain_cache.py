#!/usr/bin/env -S uv run --script
"""Jobs that add Rust targets mid-job don't use setup-soldr's toolchain cache.

The lint, rust-test and cross lanes of ci/local_gate.py run `soldr rustup
target add` during the job (x86_64-pc-windows-gnu, the cross targets).
setup-soldr's solo-toolchain-cache keys the entry on declared targets only
(`tnone` here) and saves its file list from before the build with rustup's
records from after it (zackees/setup-soldr#507). The saved entry then claims
the target is installed without its std, `target add` says "up to date", and
every later run fails with E0463 "can't find crate for `core`"
(obs-rust/obs-studio#969). Until #507 is fixed, those jobs install the
toolchain fresh (a few seconds) instead of restoring it.

    uv run python -m unittest ci/test_soldr_toolchain_cache.py
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github/workflows/build-project.yaml"
TARGET_LANES = ("lint", "rust-test", "cross")


def jobs(text: str) -> dict[str, str]:
    """Split the workflow's `jobs:` mapping into {job id: job text}."""
    body = text.split("\njobs:\n", 1)[1]
    parts = re.split(r"^  ([A-Za-z0-9_-]+):\s*$", body, flags=re.M)
    return dict(zip(parts[1::2], parts[2::2]))


def setup_soldr_steps(job: str) -> list[str]:
    """Each setup-soldr step's text, up to the next step."""
    steps = re.split(r"^      - ", job, flags=re.M)
    return [s for s in steps if "zackees/setup-soldr@" in s]


class SoldrToolchainCacheTest(unittest.TestCase):
    def test_target_adding_jobs_disable_the_toolchain_cache(self):
        found = 0
        for name, job in jobs(WORKFLOW.read_text()).items():
            lanes = set(re.findall(r"local_gate\.py --lane ([a-z0-9-]+)", job))
            if not lanes & set(TARGET_LANES):
                continue
            found += 1
            for step in setup_soldr_steps(job):
                with self.subTest(job=name):
                    self.assertRegex(step, r"\n\s+solo-toolchain-cache: false\b")
        self.assertGreaterEqual(found, 2, "expected the Rust Tests and cross jobs")


if __name__ == "__main__":
    unittest.main()
