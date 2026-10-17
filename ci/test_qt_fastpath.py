#!/usr/bin/env -S uv run --script
"""The Linux Qt fast-path job pins its prebuilt libs by tag and sha256 (#992).

    uv run python -m unittest ci/test_qt_fastpath.py
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = (ROOT / ".github/workflows/build-project.yaml").read_text()


class QtFastPathTest(unittest.TestCase):
    def job(self) -> str:
        m = re.search(r"\n  linux-qt-frontend:\n(.*?)(?=\n  [a-z0-9-]+:\n|\Z)", WORKFLOW, re.S)
        self.assertIsNotNone(m, "linux-qt-frontend job missing")
        return m.group(1)

    def test_pins(self):
        job = self.job()
        for tag, asset in [("qt6-6.11.1", "qt6-6.11.1-linux-x86_64-glibc2.28-release.tar.zst"),
                           ("ffmpeg-8.1.2", "ffmpeg-8.1.2-linux-x86_64-glibc2.28-release.tar.zst")]:
            self.assertIn(tag, job)
            self.assertIn(asset, job)
        self.assertEqual(len(set(re.findall(r"\b[0-9a-f]{64}\b", job))), 2)

    def test_caches_by_sha_and_skips_out_of_scope(self):
        job = self.job()
        self.assertIn("actions/cache@", job)
        self.assertRegex(job, r"key: .*(QT_SHA256|\b[0-9a-f]{64}\b)")
        self.assertIn("needs.check-event.outputs.qt", job)

    def test_no_distro_qt(self):
        self.assertNotRegex(self.job(), r"qt6-(base|svg)-dev")


if __name__ == "__main__":
    unittest.main()
