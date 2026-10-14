#!/usr/bin/env -S uv run --script
"""cmake/common/versionconfig.cmake derives the OBS version from git tags.

    uv run python -m unittest ci/test_versionconfig.py

The repo also carries non-OBS tags (obs-studio-rust-v*, and the retired obs-stream-rust-v0.1.0).
When one of them is the nearest tag, `git describe --tags` must not pick it,
or project(VERSION ...) fails with 'VERSION "obs-studio-rust-v0.1.0" format
invalid' (every macOS build, 2026-10-11).
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSIONCONFIG = ROOT / "cmake" / "common" / "versionconfig.cmake"


class VersionConfigTest(unittest.TestCase):
    def setUp(self):
        self.repo = Path(tempfile.mkdtemp(prefix="versionconfig-"))
        self.git("init", "-q")
        self.commit("first")

    def tearDown(self):
        shutil.rmtree(self.repo)

    def git(self, *args: str) -> None:
        subprocess.run(
            ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "tag.gpgSign=false", "-c", "commit.gpgSign=false", *args], cwd=self.repo, check=True, capture_output=True
        )

    def commit(self, msg: str) -> None:
        self.git("commit", "-q", "--allow-empty", "-m", msg)

    def version(self) -> str:
        driver = self.repo / "driver.cmake"
        driver.write_text(
            "set(_obs_default_version 0 0 1)\n"
            f'include("{VERSIONCONFIG.as_posix()}")\n'
            'message(STATUS "OBS_VERSION_CANONICAL=${OBS_VERSION_CANONICAL}")\n'
        )
        self.git("add", "driver.cmake")
        self.commit("driver")
        out = subprocess.run(["cmake", "-P", "driver.cmake"], cwd=self.repo, capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        for line in out.stdout.splitlines():
            if "OBS_VERSION_CANONICAL=" in line:
                return line.split("=", 1)[1]
        self.fail(out.stdout)

    def test_obs_tag(self):
        self.git("tag", "33.0.0-beta6")
        self.assertEqual(self.version(), "33.0.0")

    def test_newer_rust_app_tags_are_ignored(self):
        self.git("tag", "33.0.0-beta6")
        self.commit("later")
        self.git("tag", "obs-stream-rust-v0.1.0")
        self.git("tag", "obs-studio-rust-v0.1.0")
        self.git("tag", "obs-studio-rust-v0.1.0-beta.20261011")
        self.assertEqual(self.version(), "33.0.0")


if __name__ == "__main__":
    unittest.main()
