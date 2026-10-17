#!/usr/bin/env -S uv run --script
"""Every plugin that requires Qt opts out before looking for it.

    uv run python -m unittest ci/test_qt_plugins_opt_out.py

The Fast Gate builds the touched Linux targets with no Qt installed
(ENABLE_FRONTEND=OFF plus the Qt plugins' own ENABLE_* options off, #992).
A plugin whose CMakeLists.txt reaches find_package(Qt6 ... REQUIRED) without
an earlier `if(NOT ENABLE_...) return()` fails that configure for every PR
(frontend-tools did, failing Fast Gate on #1016, #1031 and #1037).
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QT_REQUIRED = re.compile(r"find_package\(\s*Qt6\b[^)]*\bREQUIRED\b")
OPT_OUT = re.compile(r"if\(\s*NOT\s+ENABLE_\w+\s*\)(?:(?!endif\().)*?return\(\)", re.S)


def qt_plugins() -> list[Path]:
    return sorted(p for p in (ROOT / "plugins").glob("*/CMakeLists.txt") if QT_REQUIRED.search(p.read_text()))


class QtPluginsOptOutTest(unittest.TestCase):
    def test_qt_plugins_exist(self):
        names = {p.parent.name for p in qt_plugins()}
        self.assertIn("frontend-tools", names)

    def test_each_returns_before_requiring_qt(self):
        for path in qt_plugins():
            text = path.read_text()
            qt = QT_REQUIRED.search(text).start()
            with self.subTest(plugin=path.parent.name):
                opt_out = OPT_OUT.search(text)
                self.assertTrue(
                    opt_out and opt_out.start() < qt,
                    f"{path.relative_to(ROOT)} requires Qt6 with no earlier if(NOT ENABLE_...) return()",
                )


if __name__ == "__main__":
    unittest.main()
