#!/usr/bin/env -S uv run --script
"""Tier 2 Linux jobs reuse their apt downloads (#1055).

    uv run python -m unittest ci/test_tier2_apt_cache.py

Every Tier 2 Linux job (build + 6 shards) downloaded the same 339 MB of
.debs (~20 s) into the stock ubuntu:26.04 container. The jobs now keep the
downloads (the image's docker-clean apt config deletes them) and cache
/var/cache/apt/archives, keyed on the package list and the image digest.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

WORKFLOW = Path(__file__).resolve().parent.parent / ".github" / "workflows" / "build-project.yaml"


def job(name: str) -> str:
    text = WORKFLOW.read_text(encoding="utf-8")
    m = re.search(rf"^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9-]+:\n|\Z)", text, re.S | re.M)
    assert m, f"no job {name}"
    return m.group(1)


class AptCacheTest(unittest.TestCase):
    def test_every_tier2_linux_job_caches_apt_archives(self):
        for name in ("rust-linux-tier2-build", "rust-linux-tier2"):
            j = job(name)
            self.assertIn("docker-clean", j, name)
            self.assertIn("path: /var/cache/apt/archives", j, name)
            self.assertIn("hashFiles('rust/tools/linux-validate/apt-packages.txt')", j, name)

    def test_cache_is_restored_before_the_install(self):
        for name in ("rust-linux-tier2-build", "rust-linux-tier2"):
            j = job(name)
            self.assertLess(j.index("path: /var/cache/apt/archives"), j.index("Install Tier 2 Packages"), name)
            self.assertLess(j.index("actions/checkout@"), j.index("path: /var/cache/apt/archives"), name)

    def test_zstd_is_there_before_the_restore(self):
        # actions/cache versions an entry by its compressor: restoring before
        # zstd is installed looks for a gzip entry the zstd save never wrote,
        # so master missed its own cache (run 38888785774).
        for name in ("rust-linux-tier2-build", "rust-linux-tier2"):
            j = job(name)
            install_git = j[j.index("Install Git"):j.index("actions/checkout@")]
            self.assertIn("zstd", install_git, name)

    def test_only_master_build_job_saves(self):
        # Every PR and queue run saved its own 323 MB entry and pushed the
        # repo past its 10 GB cache limit, evicting the ccache.
        for name in ("rust-linux-tier2-build", "rust-linux-tier2"):
            j = job(name)
            self.assertNotIn("uses: actions/cache@", j, name)
            self.assertIn("actions/cache/restore@", j, name)
        self.assertNotIn("path: /var/cache/apt/archives", job("rust-linux-tier2").split("actions/cache/restore@")[1].split("Install Tier 2")[1])
        build = job("rust-linux-tier2-build")
        save = build[build.index("Save apt Downloads"):]
        self.assertIn("refs/heads/master", save.split("\n\n")[0])


if __name__ == "__main__":
    unittest.main()
