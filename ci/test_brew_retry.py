#!/usr/bin/env -S uv run --script
"""Homebrew downloads in CI are retried (obs-rust/obs-studio#634 item 4).

    uv run python -m unittest ci/test_brew_retry.py

A single failed brew download failed the gersemi job. Every `brew update`
and `brew install` in workflows and composite actions goes through
.github/scripts/retry, which reruns a failing command with backoff.
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RETRY = ROOT / ".github" / "scripts" / "retry"
BREW = re.compile(r"(?<![\w/-])brew (update|install)\b")


class BrewRetry(unittest.TestCase):
    def test_every_brew_download_is_retried(self):
        files = subprocess.run(
            ["git", "ls-files", ".github/*.yaml", ".github/*.yml"], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout.split()
        bare = []
        for name in files:
            for number, line in enumerate((ROOT / name).read_text().splitlines(), 1):
                code = line.split("#", 1)[0] if line.lstrip().startswith("#") else line
                if BREW.search(code) and "scripts/retry" not in code:
                    bare.append(f"{name}:{number}: {line.strip()}")
        self.assertEqual(bare, [])

    def run_retry(self, script: str, attempts: str = "3"):
        with tempfile.TemporaryDirectory() as tmp:
            env = {**os.environ, "RETRY_ATTEMPTS": attempts, "RETRY_DELAY": "0", "COUNT": str(Path(tmp) / "n")}
            proc = subprocess.run([str(RETRY), "sh", "-c", script], env=env, capture_output=True, text=True)
            count = Path(env["COUNT"]).read_text().count("x") if Path(env["COUNT"]).exists() else 0
            return proc, count

    def test_retries_until_success(self):
        proc, count = self.run_retry('echo x >> "$COUNT"; [ "$(wc -l < "$COUNT")" -ge 2 ]')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(count, 2)

    def test_gives_up_with_the_last_status(self):
        proc, count = self.run_retry('echo x >> "$COUNT"; exit 7')
        self.assertEqual(proc.returncode, 7)
        self.assertEqual(count, 3)
        self.assertIn("attempt 3/3", proc.stderr)

    def test_success_runs_once(self):
        proc, count = self.run_retry('echo x >> "$COUNT"')
        self.assertEqual((proc.returncode, count), (0, 1))


if __name__ == "__main__":
    unittest.main()
