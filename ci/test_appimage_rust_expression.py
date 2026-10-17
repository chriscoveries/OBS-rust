#!/usr/bin/env -S uv run --script
"""Evaluate the AppImage Rust switch with GitHub's actual expression engine.

    uv run python -m unittest ci.test_appimage_rust_expression

The expression added by #670 treats absent inputs as explicit false because
GitHub compares null/empty strings and booleans using numeric coercion.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = "@actions/expressions@0.3.61"
PROBE = r'''
import {readFileSync} from "node:fs";
import {Parser, Lexer, Evaluator, data} from "@actions/expressions";
const workflow = readFileSync(process.argv[2], "utf8");
const variable = process.argv[3];
const input = process.argv[4];
const source = workflow.match(new RegExp(variable + ":\\s*\\$\\{\\{(.*?)\\}\\}"))[1].trim();
const results = [];
for (const event of ["push", "pull_request", "schedule", "workflow_dispatch"]) {
  for (const mode of ["absent", "empty", "false", "true"]) {
    const pairs = mode === "absent" ? [] : [{
      key: input,
      value: mode === "empty" ? new data.StringData("") : new data.BooleanData(mode === "true")
    }];
    const context = new data.Dictionary(
      {key: "inputs", value: new data.Dictionary(...pairs)},
      {key: "github", value: new data.Dictionary({key: "event_name", value: new data.StringData(event)})}
    );
    const expression = new Parser(new Lexer(source).lex().tokens, ["inputs", "github"], []).parse();
    results.push({event, mode, value: new Evaluator(expression, context).evaluate().coerceString()});
  }
}
console.log(JSON.stringify(results));
'''


class AppImageRustExpressionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("node") or not shutil.which("npm"):
            raise unittest.SkipTest("GitHub expression evaluation requires Node and npm")
        scratch = tempfile.TemporaryDirectory(prefix="obs-actions-expression-")
        cls.addClassCleanup(scratch.cleanup)
        directory = Path(scratch.name)
        install = subprocess.run(
            ["npm", "install", "--prefix", str(directory), "--ignore-scripts", "--no-audit", "--no-fund", ENGINE],
            capture_output=True, text=True, timeout=180,
        )
        if install.returncode:
            raise RuntimeError(f"GitHub expression engine installation failed:\n{install.stdout}\n{install.stderr}")
        probe = directory / "probe.mjs"
        probe.write_text(PROBE)
        def evaluate(variable, input):
            result = subprocess.run(
                ["node", str(probe), str(ROOT / ".github/workflows/appimage.yaml"), variable, input],
                capture_output=True, text=True, check=True, timeout=30,
            )
            return json.loads(result.stdout)

        cls.results = evaluate("RUST_LIBOBS", "rust_libobs")
        cls.qsv_results = evaluate("QSV", "qsv")

    def test_absent_input_defaults_to_rust(self):
        for result in self.results:
            if result["mode"] in {"absent", "empty"}:
                with self.subTest(event=result["event"], mode=result["mode"]):
                    self.assertEqual(result["value"], "true")

    def test_explicit_false_keeps_c_oracle_builds(self):
        for result in self.results:
            if result["mode"] == "false":
                with self.subTest(event=result["event"]):
                    self.assertEqual(result["value"], "false")

    def test_explicit_true_enables_rust(self):
        for result in self.results:
            if result["mode"] == "true":
                with self.subTest(event=result["event"]):
                    self.assertEqual(result["value"], "true")

    def test_qsv_is_always_a_boolean(self):
        # release-rust.yaml dispatched by hand (verify_only, repair) calls this
        # workflow with the caller's event, workflow_dispatch, but qsv is not
        # a workflow_call input; the old expression gave '' and build.py
        # failed with "argument --qsv: expected true or false, got ''".
        for result in self.qsv_results:
            with self.subTest(event=result["event"], mode=result["mode"]):
                self.assertIn(result["value"], {"true", "false"})

    def test_qsv_is_only_on_when_asked(self):
        for result in self.qsv_results:
            with self.subTest(event=result["event"], mode=result["mode"]):
                self.assertEqual(result["value"], "true" if result["mode"] == "true" else "false")


if __name__ == "__main__":
    unittest.main()
