#!/usr/bin/env -S uv run --script
"""Every `git describe --tags` that names an OBS version must only consider
OBS version tags.

    uv run python -m unittest ci/test_describe_match.py

The repository also carries obs-studio-rust-v* tags (and the retired obs-stream-rust-v0.1.0).
#220 filtered cmake/common/versionconfig.cmake; the packaging scripts kept
plain `git describe --tags --long`, so macOS disk images were named
obs-studio-obs-studio-rust-v0.1.0-beta...
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCAN = (".github/scripts", ".github/actions", "cmake", "rust/tools")
DESCRIBE = re.compile(r"\bgit\b(?:\s+-C\s+\S+|\s+--?[\w-]+(?:=\S+)?)*\s+describe\b[^\n]*--tags[^\n]*")


class DescribeMatchTest(unittest.TestCase):
    def test_describe_tags_only_matches_obs_versions(self):
        unfiltered = []
        for base in SCAN:
            for path in sorted((ROOT / base).rglob("*")):
                if not path.is_file():
                    continue
                text = path.read_text(errors="ignore")
                for m in DESCRIBE.finditer(text):
                    if "--match" not in m.group(0):
                        unfiltered.append(f"{path.relative_to(ROOT)}: {m.group(0).strip()}")
        self.assertEqual(unfiltered, [])


if __name__ == "__main__":
    unittest.main()
