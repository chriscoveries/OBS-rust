"""Exercise the Rust test lane against a changed source after a warm build."""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

CI = Path(__file__).resolve().parent
_SPEC = importlib.util.spec_from_file_location("local_gate_probe_target", CI / "local_gate.py")
local_gate = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(local_gate)


class RustSourceProbeTest(unittest.TestCase):
    def test_changed_source_fails_the_rust_test_lane(self):
        scratch_root = local_gate.ROOT / "target"
        scratch_root.mkdir(exist_ok=True)
        scratch_dir = tempfile.TemporaryDirectory(prefix="obs-gate-probe-", dir=scratch_root)
        self.addCleanup(scratch_dir.cleanup)
        scratch = Path(scratch_dir.name)
        (scratch / "src").mkdir()
        (scratch / "Cargo.toml").write_text(
            '[package]\nname = "obs-gate-probe"\nversion = "0.1.0"\nedition = "2021"\n\n[workspace]\n'
        )
        (scratch / "Cargo.lock").write_text('version = 3\n\n[[package]]\nname = "obs-gate-probe"\nversion = "0.1.0"\n')
        (scratch / "rust-toolchain.toml").write_bytes((local_gate.ROOT / "rust-toolchain.toml").read_bytes())
        source = scratch / "src" / "lib.rs"
        source.write_text("pub fn ready() {}\n")

        output = []

        def run_in_scratch(cmd, **kw):
            result = subprocess.run(
                cmd, cwd=scratch, env=kw.get("env"), capture_output=True, text=True, timeout=90, check=False
            )
            output.append(result.stdout + result.stderr)
            result.check_returncode()

        with mock.patch.object(local_gate, "run", side_effect=run_in_scratch):
            local_gate.lane_rust_test(False)
            source.write_text(source.read_text() + 'compile_error!("GATE-SOURCE-PROBE");\n')
            with self.assertRaises(subprocess.CalledProcessError):
                local_gate.lane_rust_test(False)

        self.assertIn("GATE-SOURCE-PROBE", output[-1])


if __name__ == "__main__":
    unittest.main()
