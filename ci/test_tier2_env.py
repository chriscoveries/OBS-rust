#!/usr/bin/env -S uv run --script
"""Tier 2 Linux gives the tests the same environment in Docker and natively (#982).

    uv run python -m unittest ci/test_tier2_env.py

validate.sh used to inherit whatever the harness gave it: root with /root
and no locale in the container, the `runner` user with a lived-in HOME,
C.UTF-8 and the runner image's /usr/local/bin cmake on the native runner.
It now pins the locale, timezone and PATH order itself, runs ctest with a
fresh HOME and XDG dirs under the build root, and prints an environment
fingerprint so any remaining difference shows in the log.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

VALIDATE = Path(__file__).resolve().parent.parent / "rust" / "tools" / "linux-validate" / "validate.sh"


class PinnedEnvTest(unittest.TestCase):
    def setUp(self):
        self.text = VALIDATE.read_text(encoding="utf-8")

    def test_locale_and_timezone_are_pinned(self):
        self.assertRegex(self.text, r"export LANG=C\.UTF-8 LC_ALL=C\.UTF-8 TZ=UTC")

    def test_distro_tools_come_first(self):
        self.assertIn('export PATH="/usr/bin:/bin:$PATH"', self.text)

    def test_ctest_runs_with_a_fresh_home(self):
        self.assertIn('test_home="$BUILD/test-home"', self.text)
        for var in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"):
            self.assertRegex(self.text, rf'{var}="\$test_home')
        # Each mode's environment starts from test_env (mode ON adds
        # LD_LIBRARY_PATH for libobs-with-rust, #1065).
        self.assertIn('mode_env=("${test_env[@]}")', self.text)
        for line in re.findall(r"^.*xvfb-run -a ctest.*$", self.text, re.M):
            self.assertIn('"${mode_env[@]}"', line, line)

    def test_environment_fingerprint_is_printed(self):
        self.assertIn("== environment:", self.text)
        for probe in ("id -un", "nproc", "cmake --version", "cc --version"):
            self.assertIn(probe, self.text)


if __name__ == "__main__":
    unittest.main()
