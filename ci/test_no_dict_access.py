#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""JSON is parsed eagerly into typed dataclasses; no dictionaries past the parse.

Checked: the AppImage packaging Python (build-aux/appimage/**/*.py). A JSON
object may only be touched inside a function named `parse_*` (the parsing
boundary). Anywhere else these fail:
  x["key"]            a string-keyed subscript
  x.get("key"), x.setdefault("key", ...), x.pop("key")   (os.environ is not JSON: allowed)
  {"key": ...}        a dict literal with string keys
  dict(...)
Same rule as obs-rust/obs-studio-libs (tests/test_no_dict_access.py).
"""

from __future__ import annotations

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The packaging scripts written to this rule; extend as others are converted.
CHECKED = sorted((ROOT / "build-aux/appimage/hostlib").glob("*.py"))


def _environ(node: ast.AST) -> bool:
    return isinstance(node, ast.Attribute) and node.attr == "environ"


def violations(path: Path) -> list[str]:
    tree = ast.parse(path.read_text(), str(path))
    boundary: set[int] = set()
    for fn in ast.walk(tree):
        if isinstance(fn, (ast.FunctionDef, ast.AsyncFunctionDef)) and fn.name.startswith("parse_"):
            boundary |= {id(n) for n in ast.walk(fn)}
    out = []
    for node in ast.walk(tree):
        if id(node) in boundary:
            continue
        where = f"{path.name}:{getattr(node, 'lineno', 0)}"
        if isinstance(node, ast.Subscript) and isinstance(node.slice, ast.Constant) and isinstance(
            node.slice.value, str
        ):
            out.append(f"{where}: subscript [{node.slice.value!r}]")
        elif (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr in ("get", "setdefault", "pop")
            and not _environ(node.func.value)
            and node.args
            and isinstance(node.args[0], ast.Constant)
            and isinstance(node.args[0].value, str)
        ):
            out.append(f"{where}: .{node.func.attr}({node.args[0].value!r})")
        elif isinstance(node, ast.Dict) and any(isinstance(k, ast.Constant) and isinstance(k.value, str) for k in node.keys):
            out.append(f"{where}: dict literal")
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "dict":
            out.append(f"{where}: dict()")
    return out


class NoDictAccessTest(unittest.TestCase):
    def test_checked_scripts(self):
        self.assertTrue(CHECKED, "nothing to check")
        found = [v for p in CHECKED for v in violations(p)]
        self.assertEqual(found, [], "dictionaries outside a parse_* function:\n" + "\n".join(found))

    def test_the_check_catches_each_form_and_spares_the_boundary(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "bad.py"
            bad.write_text(
                'import os\n'
                'def parse_doc(raw):\n    return raw["ok"], raw.get("ok")\n'
                'a = x["urls"]\nb = x.get("urls")\nc = {"k": 1}\nd = dict(k=1)\ne = os.environ.get("CC")\n'
            )
            kinds = sorted(v.split(": ", 1)[1] for v in violations(bad))
            self.assertEqual(kinds, [".get('urls')", "dict literal", "dict()", "subscript ['urls']"])


if __name__ == "__main__":
    sys.exit(unittest.main())
