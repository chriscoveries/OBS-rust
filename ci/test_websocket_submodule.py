#!/usr/bin/env -S uv run --script
"""plugins/obs-websocket must point at a commit that has the project fork's
bounded websocket parser (#99).

    uv run python -m unittest ci/test_websocket_submodule.py

#492 moved the submodule back to upstream 073a294cd from a stale checkout
and silently undid the #99 fix. The pointer must be 7d89109c9 or a
descendant of it.
"""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUBMODULE = "plugins/obs-websocket"
BOUNDED_PARSER = "7d89109c90a60ed119cf38fa4e35eae774959d7f"
# Upstream commits without the fix that stale checkouts have committed (#492, #496).
KNOWN_BAD = {"073a294cd0646fa1744b0ebf3293bfa63a869b50"}


def git(*args: str, cwd: Path = ROOT) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=False)


class WebsocketSubmoduleTest(unittest.TestCase):
    def test_pointer_keeps_the_bounded_parser(self):
        tree = git("ls-tree", "HEAD", SUBMODULE).stdout.split()
        self.assertEqual(tree[:2], ["160000", "commit"], f"{SUBMODULE} is not a submodule")
        pointer = tree[2]
        self.assertNotIn(pointer, KNOWN_BAD, f"{SUBMODULE} points at upstream {pointer[:9]}, without the bounded parser (#99)")
        if pointer == BOUNDED_PARSER:
            return
        sub = ROOT / SUBMODULE
        if not (sub / ".git").exists():
            self.skipTest(f"{SUBMODULE} is not checked out; cannot check ancestry of {pointer}")
        has = git("merge-base", "--is-ancestor", BOUNDED_PARSER, pointer, cwd=sub)
        if has.returncode != 0 and git("rev-parse", "--is-shallow-repository", cwd=sub).stdout.strip() == "true":
            # A shallow checkout may stop above BOUNDED_PARSER, so a miss here proves nothing.
            self.skipTest(f"{SUBMODULE} history is shallow; cannot check ancestry of {pointer[:9]}")
        self.assertEqual(
            has.returncode,
            0,
            f"{SUBMODULE} points at {pointer[:9]}, which lacks the bounded parser {BOUNDED_PARSER[:9]} (#99)",
        )


if __name__ == "__main__":
    unittest.main()
