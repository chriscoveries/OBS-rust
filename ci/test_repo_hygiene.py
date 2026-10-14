#!/usr/bin/env -S uv run --script
"""Generated files stay out of the repository.

    uv run python -m unittest ci/test_repo_hygiene.py

./lint runs the ci/ tests, which leaves CPython bytecode in ci/__pycache__.
"""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)


class RepoHygieneTest(unittest.TestCase):
    def test_bytecode_is_ignored(self):
        for path in ("ci/__pycache__/local_gate.cpython-313.pyc", "ci/local_gate.pyc"):
            self.assertEqual(git("check-ignore", "-q", "--no-index", path).returncode, 0, f"{path} is not ignored")

    def test_no_bytecode_is_tracked(self):
        tracked = git("ls-files", "*.pyc", "**/__pycache__/**").stdout.split()
        self.assertEqual(tracked, [])


if __name__ == "__main__":
    unittest.main()
