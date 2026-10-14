#!/usr/bin/env -S uv run --script
"""Release-step shell pitfalls that only fail on a real release run.

The 2026-10-14 beta (release-rust run 38492393948) built every platform and
then failed every package check:
- Windows: `$Run = if (...) { @('--run') } else { @() }` unwraps the array
  to a string, and `@Run` splatted it as `- - r u n`.
- macOS: `PlistBuddy ... /dev/stdin` cannot read a pipe ("Cannot parse a NULL
  or zero-length data").

    uv run python -m unittest ci/test_release_step_pitfalls.py
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILES = sorted([*ROOT.glob(".github/workflows/*.y*ml"), *ROOT.glob(".github/actions/*/action.y*ml")])


class ReleaseStepPitfallsTest(unittest.TestCase):
    def test_splatted_arrays_are_not_unwrapped_by_if(self):
        bad = []
        for path in FILES:
            text = path.read_text()
            for m in re.finditer(r"^\s*\$(\w+)\s*=\s*if\s*\(", text, re.M):
                if re.search(r"@" + m.group(1) + r"\b", text):
                    bad.append(f"{path.relative_to(ROOT)}: ${m.group(1)} = if (...) then @{m.group(1)}")
        self.assertEqual(bad, [], "wrap it: $X = @(if (...) { ... })")

    def test_plistbuddy_does_not_read_stdin(self):
        bad = [
            str(p.relative_to(ROOT))
            for p in FILES
            if re.search(r"PlistBuddy[^\n]*/dev/stdin", p.read_text())
        ]
        self.assertEqual(bad, [], "PlistBuddy cannot read a pipe; use plutil -extract ... -o - -")


    def test_continuous_drops_superseded_assets(self):
        # AppImage names carry the commit, so --clobber never replaces the
        # previous day's: continuous kept the 2026-10-13 AppImage and .zsync
        # next to 2026-10-14's, and AppImageUpdate matches OBS_Studio-*.zsync.
        text = (ROOT / ".github" / "workflows" / "release-rust.yaml").read_text()
        step = text.split("gh release upload continuous", 1)[1].split("gh release edit continuous", 1)[0]
        self.assertIn("gh release delete-asset continuous", step)


    def test_daily_beta_is_the_latest_release(self):
        # Published as pre-releases, the betas never showed as the repo's
        # "Latest" release; the front page still offered obs-stream-rust v0.1.0.
        text = (ROOT / ".github" / "workflows" / "release-rust.yaml").read_text()
        for m in re.finditer(r'gh release (?:create|edit) "\$TAG"[^\n]*(?:\\\n[^\n]*)*', text):
            self.assertIsNone(re.search(r"--prerelease(?!=false)", m.group(0)), m.group(0))
            self.assertIn("--latest", m.group(0))


if __name__ == "__main__":
    unittest.main()
