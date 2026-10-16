#!/usr/bin/env -S uv run --script
"""build-aux/appimage/build.py runs the workflow's AppImage build steps (#899).

    uv run python -m unittest ci/test_appimage_build_script.py

- The scripts are the workflow's former inline `run:` blocks, byte for byte
  (checked against the last workflow that had them, when git has that commit).
- The workflow calls every step, in order, and passes every input.
- --dry-run prints the plan; --skip-deps drops only the dependency install.
- version + configure, run against a stub cmake, pass the flags through.
"""

from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "build-aux/appimage/build.py"
WORKFLOW = ROOT / ".github/workflows/appimage.yaml"
# origin/master just before the move: the last appimage.yaml with inline scripts.
BEFORE = "2c38ec153b622ec6873b90b8ebe5c359ff3d4129"
OLD_STEPS = {
    "deps": "Install build dependencies",
    "libavutil": "Build libavutil with host VA compatibility",
    "version": "Determine version",
    "configure": "Configure",
    "build": "Build 🧱",
    "appdir": "Assemble AppDir",
    "appimage": "Create AppImage",
}

spec = importlib.util.spec_from_file_location("appimage_build", SCRIPT)
build = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = build
spec.loader.exec_module(build)


def run_script(*args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(["uv", "run", "--no-project", str(SCRIPT), *args],
                          capture_output=True, text=True, env=env, timeout=120)


def old_run_block(workflow: str, name: str) -> str:
    block = workflow.split(f"      - name: {name}", 1)[1].split("\n      - ", 1)[0]
    return textwrap.dedent(block.split("        run: |\n", 1)[1])


class BuildScriptTest(unittest.TestCase):
    def test_scripts_are_the_former_inline_workflow_steps(self):
        old = subprocess.run(["git", "show", f"{BEFORE}:.github/workflows/appimage.yaml"],
                             cwd=ROOT, capture_output=True, text=True)
        if old.returncode:
            self.skipTest(f"{BEFORE} not in this clone")
        args = build.parse_args(["--sha", "0" * 40])
        for step in build.plan(args):
            with self.subTest(step=step.name):
                expected = old_run_block(old.stdout, OLD_STEPS[step.name])
                # The only expression in a script became an environment variable.
                expected = expected.replace("${{ inputs.publish_continuous }}", "${PUBLISH_CONTINUOUS}")
                self.assertEqual(step.script.strip("\n"), expected.strip("\n"))

    def test_workflow_calls_every_step_in_order(self):
        called = []
        for line in WORKFLOW.read_text().splitlines():
            if "build-aux/appimage/build.py --steps " in line:
                called += line.split("--steps ", 1)[1].split()[0].split(",")
        self.assertEqual(called, list(build.STEP_NAMES))

    def test_workflow_passes_every_input(self):
        text = WORKFLOW.read_text()
        for flag, var in [("--rust-libobs", "RUST_LIBOBS"), ("--qsv", "QSV"),
                          ("--rust-version", "OBS_RUST_VERSION"), ("--publish-continuous", "PUBLISH_CONTINUOUS")]:
            with self.subTest(flag=flag):
                self.assertIn(f'{flag} "${var}"', text)
        self.assertIn("PUBLISH_CONTINUOUS: ${{ inputs.publish_continuous == true }}", text)

    def test_dry_run_prints_the_plan(self):
        result = run_script("--dry-run", "--sha", "abc")
        self.assertEqual(result.returncode, 0, result.stderr)
        headers = [l for l in result.stdout.splitlines() if l.startswith("==> ")]
        self.assertEqual([h.split("] ", 1)[1].split(":")[0] for h in headers], list(build.STEP_NAMES))
        self.assertIn("cmake --preset ubuntu-ci", result.stdout)
        self.assertIn("env RUST_LIBOBS=true", result.stdout)
        self.assertIn(f"env APPIMAGETOOL_SHA256={build.APPIMAGETOOL_SHA256}", result.stdout)

    def test_skip_deps_drops_only_the_install(self):
        result = run_script("--dry-run", "--skip-deps", "--qsv", "true", "--rust-libobs", "false")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("deps: Install build dependencies", result.stdout)
        self.assertIn("libavutil: Build libavutil", result.stdout)
        self.assertIn("env QSV=true", result.stdout)
        self.assertIn("env RUST_LIBOBS=false", result.stdout)

    def test_rejects_unknown_steps_and_values(self):
        self.assertNotEqual(run_script("--dry-run", "--steps", "bogus").returncode, 0)
        self.assertNotEqual(run_script("--dry-run", "--rust-libobs", "yes").returncode, 0)

    def test_configure_passes_flags_to_cmake(self):
        with tempfile.TemporaryDirectory() as scratch:
            stub = Path(scratch) / "cmake"
            log = Path(scratch) / "cmake.log"
            stub.write_text(f'#!/bin/sh\nprintf "%s\\n" "$@" > "{log}"\n')
            stub.chmod(0o755)
            output = Path(scratch) / "output"
            env = {**os.environ, "PATH": f"{scratch}:{os.environ['PATH']}", "GITHUB_OUTPUT": str(output)}
            result = run_script("--steps", "version,configure", "--rust-libobs", "false", "--qsv", "true",
                                "--sha", "0123456789abcdef", env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            argv = log.read_text().splitlines()
            self.assertEqual(argv[:2], ["--preset", "ubuntu-ci"])
            self.assertIn("-DENABLE_RUST_LIBOBS=FALSE", argv)
            self.assertIn("-DENABLE_QSV11=TRUE", argv)
            version = next(a for a in argv if a.startswith("-DOBS_VERSION_OVERRIDE="))
            self.assertRegex(version, r"=(\d+\.\d+\.\d+.*|0\.0\.0-g012345678)$")
            self.assertEqual(output.read_text(), "rust=FALSE\n")


if __name__ == "__main__":
    sys.exit(unittest.main())
