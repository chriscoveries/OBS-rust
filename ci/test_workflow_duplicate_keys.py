#!/usr/bin/env -S uv run --script
"""No workflow or action YAML repeats a key in the same mapping.

Two PRs each added `rust_libobs` to appimage.yaml's workflow_call inputs (one
at the top, one at the bottom); git merged both without a conflict, and
GitHub then refused every release dispatch: "'rust_libobs' is already
defined". Each PR passed on its own, so only a check of the merged file
catches it. This scans block-style YAML by indentation (stdlib only).

    uv run python -m unittest ci/test_workflow_duplicate_keys.py
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILES = sorted([*ROOT.glob(".github/workflows/*.y*ml"), *ROOT.glob(".github/actions/*/action.y*ml")])
KEY = re.compile(r"^(?P<indent> *)(?:- +)?(?P<key>[A-Za-z0-9_.-]+|'[^']*'|\"[^\"]*\"):(?:\s|$)")


def duplicate_keys(text: str) -> list[str]:
    found = []
    # Each open mapping: (indent of its keys, keys seen). A "- " list item
    # starts a new mapping at the indent of its first key.
    stack: list[tuple[int, set[str]]] = []
    block_indent = None  # inside a | or > block scalar: skip deeper lines
    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip(" ")
        if not stripped or stripped.startswith("#"):
            continue
        indent = len(line) - len(stripped)
        if block_indent is not None:
            if indent > block_indent:
                continue
            block_indent = None
        m = KEY.match(line)
        if not m:
            if stripped.startswith("- "):
                while stack and stack[-1][0] > indent:
                    stack.pop()
            continue
        key_indent = indent + (len(stripped) - len(stripped.lstrip("- ")) if stripped.startswith("- ") else 0)
        while stack and stack[-1][0] > key_indent:
            stack.pop()
        if stripped.startswith("- ") or not stack or stack[-1][0] < key_indent:
            stack.append((key_indent, set()))
        keys = stack[-1][1]
        key = m.group("key").strip("'\"")
        if key in keys:
            found.append(f"line {lineno}: {key}")
        keys.add(key)
        if re.search(r":\s*[|>][-+0-9]*\s*(#.*)?$", line):
            block_indent = indent
    return found


class WorkflowDuplicateKeysTest(unittest.TestCase):
    def test_no_duplicate_keys(self):
        bad = []
        for path in FILES:
            for d in duplicate_keys(path.read_text()):
                bad.append(f"{path.relative_to(ROOT)} {d}")
        self.assertEqual(bad, [])

    def test_detects_a_duplicate_input(self):
        text = "on:\n  workflow_call:\n    inputs:\n      a:\n        type: string\n      a:\n        type: boolean\n"
        self.assertEqual(duplicate_keys(text), ["line 6: a"])

    def test_list_items_are_separate_mappings(self):
        text = "steps:\n  - name: x\n    run: |\n      name: y\n  - name: z\n"
        self.assertEqual(duplicate_keys(text), [])


if __name__ == "__main__":
    unittest.main()
