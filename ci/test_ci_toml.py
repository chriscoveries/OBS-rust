#!/usr/bin/env -S uv run --script
"""ci.toml declares every cache family and workflow trigger (#957).

    uv run python -m unittest ci/test_ci_toml.py

ci.toml is the zackees/ci.yml schema 3 contract. The cache janitor
(.github/workflows/ci-pre.yml) deletes every Actions cache whose key matches
no [cache.family], so a cache key added to a workflow without a family would
be deleted on the next PR run. [allow].workflows must list each workflow with
its exact triggers (GEN-008). `ci-lint precheck` checks the rest; it needs the
network, so this offline test covers the two things a workflow edit can break.
"""

from __future__ import annotations

import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CI_TOML = ROOT / "ci.toml"
GITHUB = ROOT / ".github"
WORKFLOWS = GITHUB / "workflows"
PIN = "1a970f01574447d681571ff3d0b8de56cb983dcc"

# Prefixes of the families a `via` names (zackees/ci.yml ci_lint/cache/families.py).
VIA_PREFIXES = {
    "setup-soldr:cargo-registry": "setup-soldr-cargoregistry-v1-",
    "setup-soldr:cook": "cook-base-v2-",
    "setup-soldr:build-cache": "setup-soldr-buildcache-v2-",
    "setup-soldr:solo-toolchain": "solo-toolchain-v3-",
    "setup-soldr:soldr-mini": "soldr-mini-v2-",
    "setup-uv": "setup-uv-2-",
}


def load() -> dict:
    return tomllib.loads(CI_TOML.read_text())


def family_prefixes(ci: dict) -> list[str]:
    out = []
    for fam in ci["cache"]["family"].values():
        out.append(fam["prefix"] if "prefix" in fam else VIA_PREFIXES[fam["via"]])
    return out


def runner_os(lines: list[str], index: int) -> str:
    """runner.os for the job around line `index` (its nearest runs-on)."""
    for line in reversed(lines[:index]):
        m = re.match(r"\s+runs-on: (.*)", line)
        if m:
            label = m.group(1)
            return "macOS" if "macos" in label else "Windows" if "windows" in label else "Linux"
    return "Linux"


def cache_keys() -> list[tuple[str, str]]:
    """(file, literal key prefix) of every actions/cache key and restore key,
    and of every flatpak-builder cache key the workflows build."""
    out = []
    files = sorted(WORKFLOWS.glob("*.y*ml")) + sorted(GITHUB.glob("actions/*/action.y*ml"))
    for path in files:
        lines = path.read_text().splitlines()
        starts = [i for i, line in enumerate(lines) if re.match(r"\s+- ", line)] + [len(lines)]
        for start, end in zip(starts, starts[1:]):
            step = lines[start:end]
            values = []
            if any(re.search(r"uses: actions/cache(/restore|/save)?@", line) for line in step):
                for k, line in enumerate(step):
                    m = re.match(r"\s+(key|restore-keys): *(.*)", line)
                    if not m:
                        continue
                    if m.group(2) in ("|", ">"):
                        for s in step[k + 1 :]:
                            if not s.strip() or s.strip().startswith("#") or re.match(r"\s+[a-z-]+:", s):
                                break
                            values.append(s.strip())
                    else:
                        values.append(m.group(2))
            values += [m.group(1) for line in step for m in [re.search(r"cache_key='([^']*)'", line)] if m]
            for value in values:
                value = value.replace("${{ runner.os }}", runner_os(lines, start))
                out.append((path.relative_to(ROOT).as_posix(), value.split("${{")[0]))
    return out


def triggers(path: Path) -> list[str]:
    text = path.read_text()
    m = re.search(r"^(?:on|'on'|\"on\"):\n((?:[ \t]+.*\n|\n)*)", text, re.M)
    assert m, f"{path.name} has no on: block"
    return re.findall(r"^  ([a-z_]+):", m.group(1), re.M)


class CiTomlTest(unittest.TestCase):
    def test_schema_and_pin(self):
        ci = load()
        self.assertEqual(ci["schema"], 3)
        self.assertEqual(ci["linter"], f"zackees/ci.yml@{PIN}")
        self.assertIn(PIN, (ROOT / "local-gate.toml").read_text())

    def test_every_cache_key_has_a_family(self):
        prefixes = family_prefixes(load())
        keys = cache_keys()
        self.assertGreater(len(keys), 10)
        for path, key in keys:
            with self.subTest(path=path, key=key):
                self.assertTrue(key, "a cache key must start with a literal prefix")
                self.assertTrue(any(key.startswith(p) for p in prefixes), f"no [cache.family] prefix matches {key!r}")

    def test_setup_soldr_and_uv_families_declared(self):
        vias = {fam.get("via") for fam in load()["cache"]["family"].values()}
        self.assertLessEqual(set(VIA_PREFIXES), vias)

    def test_workflows_declare_their_triggers(self):
        allowed = load()["allow"]["workflows"]
        files = {p.name: p for p in WORKFLOWS.glob("*.y*ml")}
        self.assertEqual(set(allowed), set(files))
        for name, path in files.items():
            with self.subTest(workflow=name):
                self.assertEqual(sorted(set(triggers(path))), sorted(set(t.split(":")[0] for t in allowed[name])))


if __name__ == "__main__":
    unittest.main()
