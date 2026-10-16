"""Exercise the Rust test lane against a changed source after a warm build."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

CI = Path(__file__).resolve().parent
_SPEC = importlib.util.spec_from_file_location("local_gate_probe_target", CI / "local_gate.py")
local_gate = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(local_gate)


CROSS_TARGET = "x86_64-pc-windows-gnu"


def is_cross_step(cmd: list[str]) -> bool:
    """The rust-test lane's windows-gnu link check (#291, #925). The probe
    skips it: the scratch crate has no windows-gnu std on CI (#971), and the
    real rust-test lane checks the cross link on the full workspace."""
    return CROSS_TARGET in cmd


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
            if is_cross_step(cmd):
                return
            result = subprocess.run(
                cmd, cwd=scratch, env=kw.get("env"), capture_output=True, text=True, timeout=90, check=False
            )
            output.append(result.stdout + result.stderr)
            if result.returncode != 0:
                # Without this the lane's failure is just "exit status 101".
                print(f"$ {' '.join(cmd)}\n{output[-1][-4000:]}", file=sys.stderr)
            result.check_returncode()

        with mock.patch.object(local_gate, "run", side_effect=run_in_scratch):
            local_gate.lane_rust_test(False)
            source.write_text(source.read_text() + 'compile_error!("GATE-SOURCE-PROBE");\n')
            with self.assertRaises(subprocess.CalledProcessError):
                local_gate.lane_rust_test(False)

        self.assertIn("GATE-SOURCE-PROBE", output[-1])


class ProbeScopeTest(unittest.TestCase):
    """The probe checks change detection on the host; it must not depend on
    the runner having the windows-gnu std, which the scratch crate lacked on
    CI (#971). The real rust-test lane checks the cross link."""

    def test_cross_steps_are_skipped_in_the_scratch_crate(self):
        self.assertTrue(is_cross_step(["soldr", "rustup", "target", "add", "x86_64-pc-windows-gnu"]))
        self.assertTrue(is_cross_step(
            ["soldr", "cargo", "test", "--workspace", "--locked", "--no-run", "--target", "x86_64-pc-windows-gnu"]))
        self.assertFalse(is_cross_step(["soldr", "cargo", "test", "--workspace", "--locked"]))


if __name__ == "__main__":
    unittest.main()
