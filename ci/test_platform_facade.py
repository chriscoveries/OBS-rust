#!/usr/bin/env -S uv run --script
"""The frontend platform facade has one implementation per platform (#849).

    uv run python -m unittest ci/test_platform_facade.py

frontend/utility/platform.hpp declares the OBSApp facade between the marker
comment below and the next declaration group. Each function declared there
must be defined in platform-osx.mm, platform-windows.cpp and platform-x11.cpp
(Linux and BSD), and frontend/OBSApp.cpp must call it, so a platform cannot
lose its branch when code moves out of the shared file. Only the native macOS
and Windows builds compile those files; this keeps a missing definition from
waiting for them.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

import platform_ifdef_ratchet as ratchet  # noqa: E402

UTILITY = ROOT / "frontend" / "utility"
HEADER = UTILITY / "platform.hpp"
IMPLEMENTATIONS = ("platform-osx.mm", "platform-windows.cpp", "platform-x11.cpp")
MARKER = "OBSApp platform facade (#849)"
DECLARATION = re.compile(r"^[A-Za-z_][\w:<>\s\*&,]*?\b(\w+)\([^;{]*\);$")


def facade_functions() -> list[str]:
    text = HEADER.read_text()
    start = text.index(MARKER)
    end = text.index("enum TaskbarOverlayStatus", start)
    names = []
    for line in text[start:end].splitlines():
        match = DECLARATION.match(line.strip())
        if match:
            names.append(match.group(1))
    return names


def defines(text: str, name: str) -> bool:
    return re.search(rf"^[A-Za-z_][\w:<>\s\*&,]*?\b{name}\([^;{{]*\)\s*(\{{|$)", text, re.M) is not None


class FacadeTest(unittest.TestCase):
    def test_facade_is_declared(self):
        names = facade_functions()
        self.assertGreaterEqual(len(names), 19, names)
        self.assertIn("InitPlatformConfigDefaults", names)
        self.assertIn("GetPlatformVersionName", names)

    def test_every_platform_defines_every_function(self):
        names = facade_functions()
        for impl in IMPLEMENTATIONS:
            text = (UTILITY / impl).read_text()
            for name in names:
                with self.subTest(file=impl, function=name):
                    self.assertTrue(defines(text, name), f"{impl} does not define {name}")

    def test_obsapp_calls_the_facade(self):
        text = (ROOT / "frontend" / "OBSApp.cpp").read_text()
        for name in facade_functions():
            with self.subTest(function=name):
                self.assertRegex(text, rf"\b{name}\(", f"OBSApp.cpp never calls {name}")

    def test_implementations_include_the_util_headers_they_call(self):
        # Code moved out of OBSApp.cpp lost the includes it relied on there;
        # only the native builds would notice (#987: astrcmpi in platform-osx.mm
        # and platform-windows.cpp without <util/dstr.h>).
        headers = {r"\bastrcmpi?\(|\bastrcmp_n\(|\bdstr_\w+\(": "util/dstr.h"}
        for impl in IMPLEMENTATIONS:
            text = (UTILITY / impl).read_text()
            for call, header in headers.items():
                if re.search(call, text):
                    with self.subTest(file=impl, header=header):
                        self.assertRegex(text, rf"#(include|import)\s+[<\"]{re.escape(header)}[>\"]")

    def test_obsapp_has_no_platform_conditionals(self):
        text = (ROOT / "frontend" / "OBSApp.cpp").read_text()
        self.assertEqual(ratchet.count_platform_conditionals(text), [])


if __name__ == "__main__":
    unittest.main()
