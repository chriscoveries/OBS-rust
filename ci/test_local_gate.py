#!/usr/bin/env -S uv run --script
"""Unit tests for ci/local_gate.py.

    uv run python -m unittest ci/test_local_gate.py
"""

from __future__ import annotations

import importlib.util
import os
import subprocess
import unittest
from pathlib import Path
from unittest import mock

_SPEC = importlib.util.spec_from_file_location("local_gate", Path(__file__).with_name("local_gate.py"))
local_gate = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(local_gate)


class SoldrCacheTest(unittest.TestCase):
    """soldr's zccache can replay stale rustc output (obs-rust/obs-studio#134),
    so every cargo command the gate attests must bypass the rustc wrapper."""

    def cargo_calls(self, lane, env=None):
        calls = []

        def fake_run(cmd, **kw):
            calls.append((cmd, kw.get("env")))
            return subprocess.CompletedProcess(cmd, 0)

        with (
            mock.patch.dict(os.environ, env or {}, clear=False),
            mock.patch.object(local_gate.subprocess, "run", side_effect=fake_run),
            mock.patch.object(local_gate, "run", side_effect=fake_run),
            mock.patch.object(local_gate, "tracked_files", return_value=[]),
            mock.patch.object(local_gate, "clang_format"),
            mock.patch.object(local_gate, "gersemi"),
            mock.patch.object(local_gate, "self_test", create=True),
            mock.patch.object(local_gate, "toolchain_gcc_ld", return_value="/sysroot/gcc-ld"),
        ):
            # The caller's own setting must not leak into the check.
            if "SOLDR_RUSTC_WRAPPER" not in (env or {}):
                os.environ.pop("SOLDR_RUSTC_WRAPPER", None)
            lane(False)
        return [(cmd, e) for cmd, e in calls if cmd[:2] == ["soldr", "cargo"]]

    def test_lanes_disable_rustc_wrapper(self):
        for lane in (local_gate.lane_lint, local_gate.lane_rust_test):
            calls = self.cargo_calls(lane)
            self.assertTrue(calls, f"{lane.__name__} runs no soldr cargo command")
            for cmd, env in calls:
                with self.subTest(lane=lane.__name__, cmd=" ".join(cmd)):
                    self.assertIsNotNone(env, "cargo runs with the caller's environment")
                    self.assertEqual(env.get("SOLDR_RUSTC_WRAPPER"), "none")

    def test_opt_in_keeps_wrapper(self):
        # OBS_GATE_ZCCACHE=1 restores the cached build for local iteration;
        # the caller's SOLDR_RUSTC_WRAPPER is then left alone.
        for cmd, env in self.cargo_calls(local_gate.lane_rust_test, {"OBS_GATE_ZCCACHE": "1"}):
            self.assertNotEqual((env or {}).get("SOLDR_RUSTC_WRAPPER"), "none", " ".join(cmd))


class SelfTestTest(unittest.TestCase):
    def test_runs_every_ci_test_module(self):
        # Every ci/test_*.py, not just this file, so new checks (tier 2
        # volume, toolchain pin, registration fragments) run in the gate.
        calls = []
        with mock.patch.object(local_gate, "run", side_effect=lambda cmd, **kw: calls.append(cmd)):
            local_gate.self_test()
        self.assertEqual(len(calls), 1)
        cmd = calls[0]
        self.assertIn("discover", cmd)
        self.assertIn("test_*.py", cmd)


class EnvironmentHintTest(unittest.TestCase):
    """Environment failures get a one-line hint naming the documented fix
    (obs-rust/obs-studio#694, docs/rust-port/troubleshooting.md)."""

    CASES = {
        "soldr: IO error: Permission denied (os error 13)": "ownership",
        "soldr: warning: disk space is low (6.2 GiB free). Run `soldr gc` to review": "soldr gc",
        "Could NOT find FFmpeg (missing: avcodec) (Required is at least version \"8.0\")": "./unittest",
        "Cannot connect to the Docker daemon at unix:///var/run/docker.sock": "Docker",
    }

    def test_each_error_maps_to_a_hint(self):
        for line, words in self.CASES.items():
            with self.subTest(line=line):
                hints = local_gate.hints_for(line)
                self.assertEqual(len(hints), 1, hints)
                self.assertIn(words, hints[0])
                self.assertIn("troubleshooting.md", hints[0])

    def test_ordinary_output_has_no_hint(self):
        self.assertEqual(local_gate.hints_for("test result: ok. 12 passed\nerror[E0308]: mismatched types"), [])

    def test_hints_are_not_repeated(self):
        line = "soldr: IO error: Permission denied (os error 13)"
        self.assertEqual(len(local_gate.hints_for(f"{line}\n{line}")), 1)

    def test_missing_soldr_fails_the_lane_with_a_hint(self):
        def missing(cmd, **kw):
            raise FileNotFoundError(2, "No such file or directory", "soldr")

        out = []
        with mock.patch.object(local_gate.subprocess, "Popen", side_effect=missing), \
                mock.patch("builtins.print", side_effect=lambda *a, **k: out.append(" ".join(map(str, a)))), \
                mock.patch("sys.argv", ["local_gate.py", "--lane", "rust-test"]):
            self.assertEqual(local_gate.main(), 1)
        text = "\n".join(out)
        self.assertIn("lane rust-test: FAILED", text)
        self.assertIn("soldr is not on PATH", text)
        self.assertIn("troubleshooting.md", text)

    def test_run_streams_output_and_reports_hints(self):
        out = []
        with mock.patch("builtins.print", side_effect=lambda *a, **k: out.append(" ".join(map(str, a)))):
            with self.assertRaises(subprocess.CalledProcessError):
                local_gate.run(["sh", "-c", "echo 'soldr: IO error: Permission denied (os error 13)'; exit 3"])
        text = "\n".join(out)
        self.assertIn("Permission denied", text)
        self.assertIn("hint:", text)


if __name__ == "__main__":
    unittest.main()


class ClangFormatScopeTest(unittest.TestCase):
    """CI's clang-format job checks every changed C/C++/ObjC file
    (.github/actions/run-clang-format, checkGlob '*.c' '*.h' ...), but the
    lint lane only formatted the upstream OBS directories. C under rust/
    (obs-c-oracle wrappers, the platform_windows SEH shim) passed the local
    gate and then failed CI's clang-format (#707)."""

    def test_rust_c_sources_are_checked(self):
        files = [
            "rust/obs-util/src/platform/windows/plugin_seh.c",
            "rust/obs-c-oracle/oracle/platform_windows.c",
            "libobs/obs.c",
        ]
        checked = [
            f
            for f in files
            if local_gate.top_dir_matches(f, local_gate.CLANG_DIRS)
            and f.endswith(local_gate.CLANG_EXTS)
            and not local_gate.excluded(f, local_gate.CLANG_EXCLUDE)
        ]
        self.assertEqual(checked, files)
