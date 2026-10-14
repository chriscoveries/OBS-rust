#!/usr/bin/env -S uv run --script
"""cmocka tests get per-test sanitizers only through obs_test_sanitize (#159).

    uv run python -m unittest ci/test_cmocka_sanitize.py

A raw -fsanitize= on a test target breaks any build configured with another
sanitizer ("-fsanitize=thread is incompatible with -fsanitize=address") and
leaves binaries valgrind can't run. obs_test_sanitize() in
test/cmocka/CMakeLists.txt adds them only when the build isn't already
sanitized and OBS_TEST_SANITIZE_PER_TARGET is ON.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPER = re.compile(r"^function\(obs_test_sanitize\b.*?^endfunction\(\)", re.M | re.S)


class CmockaSanitizeTest(unittest.TestCase):
    def test_no_raw_sanitizer_flags(self):
        cmocka = ROOT / "test" / "cmocka"
        found = []
        for path in [cmocka / "CMakeLists.txt", *sorted((cmocka / "tests.d").glob("*.cmake"))]:
            text = HELPER.sub("", path.read_text())
            for number, line in enumerate(text.splitlines(), 1):
                if "-fsanitize" in line and not line.lstrip().startswith("#"):
                    found.append(f"{path.relative_to(ROOT)}: {line.strip()}")
        self.assertEqual(found, [])

    def test_helper_exists(self):
        self.assertRegex((ROOT / "test/cmocka/CMakeLists.txt").read_text(), HELPER)


if __name__ == "__main__":
    unittest.main()
