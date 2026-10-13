#!/usr/bin/env python3
"""The C oracle's stand-ins for libobs functions match libobs (#145).

    python3 -m unittest ci/test_oracle_standins.py

Tier 3 parity is only as good as the oracle. rust/obs-c-oracle/oracle/
test_stubs.c and *_host.c re-implement libobs functions the oracle needs,
and when a copy drifts from libobs, parity certifies the difference
(#77, #79, #84). Every function defined there that libobs also defines must
be identical to one libobs definition (comments and whitespace ignored),
or be listed in DIFFERENT with the reason it may differ.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORACLE = ROOT / "rust" / "obs-c-oracle" / "oracle"
STANDINS = tuple(sorted(p.name for p in ORACLE.glob("*_host.c")))
DEFINITION = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \t*]*?\b([a-z_][a-z0-9_]*)\s*\([^;{}]*\)\s*\{", re.M
)

# Stand-ins that intentionally differ from libobs, and why.
# Windows-only forwarders in platform_host.c: the Rust crates declare these on
# Windows, the oracle's only copies are the oracle_-renamed ones platform.c
# compiles, so they forward to those instead of repeating the bodies. Unused on
# Unix, where the oracle itself calls the oracle_ names.
WINDOWS_FORWARDER = "Windows forwarder to the oracle_ copy from oracle/platform.c"

DIFFERENT = {
    "os_gettime_ns": "test-only zero clock where no Rust shim exists (macOS, Windows)",
    "os_breakpoint": WINDOWS_FORWARDER,
    "os_oom": WINDOWS_FORWARDER,
    "os_mbs_to_wcs": WINDOWS_FORWARDER,
    "os_utf8_to_wcs": WINDOWS_FORWARDER,
    "os_wcs_to_utf8": WINDOWS_FORWARDER,
    "os_wcs_to_mbs": WINDOWS_FORWARDER,
    "os_mbs_to_wcs_ptr": WINDOWS_FORWARDER,
    "os_utf8_to_wcs_ptr": WINDOWS_FORWARDER,
    "os_wcs_to_utf8_ptr": WINDOWS_FORWARDER,
    "os_mbs_to_utf8_ptr": WINDOWS_FORWARDER,
    "os_fopen": WINDOWS_FORWARDER,
    "os_fseeki64": WINDOWS_FORWARDER,
    "os_ftelli64": WINDOWS_FORWARDER,
}


def functions(text: str) -> dict[str, list[str]]:
    found: dict[str, list[str]] = {}
    for match in DEFINITION.finditer(text):
        depth = 0
        for end in range(match.end() - 1, len(text)):
            if text[end] == "{":
                depth += 1
            elif text[end] == "}":
                depth -= 1
                if depth == 0:
                    break
        found.setdefault(match.group(1), []).append(text[match.start() : end + 1])
    return found


def normalized(code: str) -> str:
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", code, flags=re.S)
    return re.sub(r"\s+", " ", code).strip()


def libobs_functions() -> dict[str, set[str]]:
    found: dict[str, set[str]] = {}
    for path in sorted((ROOT / "libobs").rglob("*.c")):
        for name, bodies in functions(path.read_text(errors="ignore")).items():
            found.setdefault(name, set()).update(normalized(b) for b in bodies)
    return found


class OracleStandinTest(unittest.TestCase):
    def test_standins_match_libobs(self):
        libobs = libobs_functions()
        drifted = []
        for file in STANDINS:
            for name, bodies in functions((ORACLE / file).read_text()).items():
                if name not in libobs or name in DIFFERENT:
                    continue
                for body in bodies:
                    if normalized(body) not in libobs[name]:
                        drifted.append(f"{file}: {name}")
        self.assertEqual(drifted, [], "copy libobs exactly, or list the function in DIFFERENT with a reason")

    def test_different_entries_are_still_standins(self):
        defined = set()
        for file in STANDINS:
            defined.update(functions((ORACLE / file).read_text()))
        self.assertEqual(sorted(set(DIFFERENT) - defined), [], "remove stale DIFFERENT entries")


if __name__ == "__main__":
    unittest.main()
