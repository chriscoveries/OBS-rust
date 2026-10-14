#!/usr/bin/env -S uv run --script
"""The obs-deps download retries before failing the configure.

A single network timeout on one dependency zip dropped Windows from the
2026-10-13 daily beta (release-rust run 38433764823). These run
_obs_download_dependency from cmake/common/buildspec_common.cmake with
`cmake -P` against file:// URLs, so they need no network.

    uv run python -m unittest ci/test_deps_download_retry.py
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULE = ROOT / "cmake" / "common" / "buildspec_common.cmake"


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class DepsDownloadRetryTest(unittest.TestCase):
    def run_download(self, tmp: Path, url: str, sha256: str) -> subprocess.CompletedProcess:
        script = tmp / "download.cmake"
        script.write_text(
            f'include("{MODULE.as_posix()}")\n'
            f'set(OBS_DEPENDENCY_DOWNLOAD_RETRY_DELAY 0)\n'
            f'_obs_download_dependency("{url}" "{(tmp / "out.zip").as_posix()}" "{sha256}")\n'
        )
        return subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)

    def test_failed_download_is_tried_three_times(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            missing = (tmp / "missing.zip").as_uri()
            res = self.run_download(tmp, missing, "0" * 64)
            out = res.stdout + res.stderr
            self.assertNotEqual(res.returncode, 0, out)
            self.assertEqual(out.count(f"Downloading {missing} (attempt"), 3, out)
            self.assertIn("Unable to download", out)
            self.assertFalse((tmp / "out.zip").exists())
            self.assertFalse((tmp / "out.zip.tmp").exists())

    def test_good_download_succeeds_once(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            src = tmp / "dep.zip"
            src.write_bytes(b"obs-deps")
            sha = hashlib.sha256(b"obs-deps").hexdigest()
            res = self.run_download(tmp, src.as_uri(), sha)
            out = res.stdout + res.stderr
            self.assertEqual(res.returncode, 0, out)
            self.assertEqual(out.count(f"Downloading {src.as_uri()} (attempt"), 1, out)
            self.assertEqual((tmp / "out.zip").read_bytes(), b"obs-deps")


    def test_wrong_hash_fails_without_installing(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            src = tmp / "dep.zip"
            src.write_bytes(b"tampered")
            res = self.run_download(tmp, src.as_uri(), hashlib.sha256(b"obs-deps").hexdigest())
            out = res.stdout + res.stderr
            self.assertNotEqual(res.returncode, 0, out)
            self.assertIn("SHA256 mismatch", out)
            self.assertFalse((tmp / "out.zip").exists())


if __name__ == "__main__":
    unittest.main()
