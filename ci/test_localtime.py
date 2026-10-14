#!/usr/bin/env -S uv run --script
"""The frontend never calls the non-reentrant localtime() (#132).

    uv run python -m unittest ci/test_localtime.py

blog() timestamps every log line from the audio, video, capture and UI
threads at once, and localtime() returns a pointer to one static buffer
shared by all of them. Use localtime_r (localtime_s on Windows).
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCAN = ("frontend",)
CALL = re.compile(r"(?<![\w.])(?:std::)?localtime\s*\(")


class LocaltimeTest(unittest.TestCase):
    def test_frontend_uses_reentrant_localtime(self):
        found = []
        for base in SCAN:
            for path in sorted((ROOT / base).rglob("*")):
                if path.suffix not in {".c", ".cpp", ".h", ".hpp", ".mm"}:
                    continue
                for number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
                    if CALL.search(line):
                        found.append(f"{path.relative_to(ROOT)}:{number}")
        self.assertEqual(found, [])


if __name__ == "__main__":
    unittest.main()
