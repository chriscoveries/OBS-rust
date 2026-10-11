#!/usr/bin/env python3
"""Unit tests for ci/local_gate.py.

    python3 -m unittest ci/test_local_gate.py
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
            mock.patch.object(local_gate, "tracked_files", return_value=[]),
            mock.patch.object(local_gate, "clang_format"),
            mock.patch.object(local_gate, "gersemi"),
            mock.patch.object(local_gate, "self_test", create=True),
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


if __name__ == "__main__":
    unittest.main()
