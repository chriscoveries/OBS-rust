#!/usr/bin/env -S uv run --script
"""The daily release restores the native compiler caches (#782).

    uv run python -m unittest ci/test_release_native_cache.py

release-rust.yaml's Windows and macOS builds never restored a compiler
cache: Windows x64 took 15m34s at 0/716 ccache hits against 3m08s at
759/761 in the scheduled build (#782). The release now restores, read-only,
the caches build-project.yaml's app builds save on master (same target and
RelWithDebInfo; the C objects the release's Rust libobs leaves alone hit),
and never saves, so it cannot grow or skew them.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RELEASE = ROOT / ".github" / "workflows" / "release-rust.yaml"
BUILD = ROOT / ".github" / "workflows" / "build-project.yaml"


def build_job() -> str:
    text = RELEASE.read_text(encoding="utf-8")
    m = re.search(r"^  build:\n(.*?)(?=^  [a-z0-9-]+:\n)", text, re.S | re.M)
    assert m
    return m.group(1)


class ReleaseNativeCacheTest(unittest.TestCase):
    def setUp(self):
        self.job = build_job()
        self.build_at = self.job.index("uses: ./.github/actions/build-obs")

    def test_windows_restores_the_app_build_ccache_and_sets_it_up(self):
        i = self.job.index("restore-keys: windows-ccache-${{ matrix.target }}-RelWithDebInfo-")
        self.assertLess(i, self.build_at)
        self.assertIn("windows-ccache-${{ matrix.architecture }}-${{ needs.check-event.outputs.config }}-", BUILD.read_text(encoding="utf-8"))
        setup = self.job[self.job.index("Set Up ccache"):self.build_at]
        self.assertIn("6219f3865ca59aec41ee4b678df171d5d35855ecb2b6dbbbd20690b3a68af7b4", setup)
        self.assertIn("CCACHE_DIR", setup)

    def test_macos_restores_the_app_build_xcode_cas(self):
        i = self.job.index("restore-keys: macOS-xcode-${{ matrix.target }}-RelWithDebInfo-")
        self.assertLess(i, self.build_at)
        self.assertIn("${{ runner.os }}-xcode-${{ matrix.target }}-${{ needs.check-event.outputs.config }}-", BUILD.read_text(encoding="utf-8"))

    def test_release_never_saves_native_caches(self):
        self.assertNotIn("actions/cache/save@", self.job)
        self.assertNotIn("uses: actions/cache@", self.job)


if __name__ == "__main__":
    unittest.main()
