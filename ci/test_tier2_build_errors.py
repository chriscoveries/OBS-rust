#!/usr/bin/env -S uv run --script
"""Tests for rust/tools/linux-validate/build-errors.sh.

    uv run python -m unittest ci/test_tier2_build_errors.py

A failed Tier 2 build printed only lines matching ": error" and "FAILED:".
cargo's messages ("error[E0308]: ...", "error: ...", a build script's
panic) match neither, so #814's ON build showed just
"FAILED: [code=101] .../libobs_preflight_ffi.a" and no reason.
"""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "rust" / "tools" / "linux-validate" / "build-errors.sh"

LOG = """\
[12/900] Building C object libobs/CMakeFiles/libobs.dir/util/dstr.c.o
/src/libobs/util/dstr.c:10:5: error: unknown type name 'foo'
FAILED: libobs/CMakeFiles/libobs.dir/util/dstr.c.o
   Compiling obs-preflight v0.1.0 (/src/rust/obs-preflight)
error[E0308]: mismatched types
  --> rust/obs-preflight/src/lib.rs:42:9
   |
42 |         "nope"
   |         ^^^^^^ expected `u32`, found `&str`

error: could not compile `obs-preflight` (lib) due to 1 previous error
thread 'main' panicked at rust/obs-preflight-ffi/build.rs:7:5:
cbindgen failed
FAILED: [code=101] obs-preflight-cargo/release/libobs_preflight_ffi.a
   Compiling obs-util v0.1.0 (/src/rust/obs-util)
warning: unused variable: `x`
"""


class BuildErrorsTest(unittest.TestCase):
    def run_script(self, log: str):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "build.log"
            p.write_text(log)
            return subprocess.run(["bash", str(SCRIPT), str(p)], capture_output=True, text=True)

    def test_keeps_c_and_ninja_errors(self):
        out = self.run_script(LOG).stdout
        self.assertIn("dstr.c:10:5: error: unknown type name 'foo'", out)
        self.assertIn("FAILED: [code=101] obs-preflight-cargo/release/libobs_preflight_ffi.a", out)

    def test_shows_rustc_errors_with_their_location(self):
        out = self.run_script(LOG).stdout
        self.assertIn("error[E0308]: mismatched types", out)
        self.assertIn("--> rust/obs-preflight/src/lib.rs:42:9", out)
        self.assertIn("could not compile `obs-preflight`", out)

    def test_shows_build_script_panics(self):
        out = self.run_script(LOG).stdout
        self.assertIn("panicked at rust/obs-preflight-ffi/build.rs:7:5", out)
        self.assertIn("cbindgen failed", out)

    def test_drops_noise(self):
        out = self.run_script(LOG).stdout
        self.assertNotIn("Compiling obs-util", out)
        self.assertNotIn("warning: unused variable", out)


if __name__ == "__main__":
    unittest.main()
