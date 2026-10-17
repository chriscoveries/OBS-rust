#!/usr/bin/env -S uv run --script
"""Cap the integration-test executables Cargo builds per crate (#968).

Every `tests/*.rs` file is its own test binary, and each one is linked on
every lane. Group them into category binaries `tests/<category>/main.rs`
(one `mod <old_name>;` per old file) instead.

    uv run ci/test_binary_cap.py --report
    uv run python -m unittest ci/test_binary_cap.py
"""

from __future__ import annotations

import sys
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PER_CRATE_CAP = 8
# The workspace total after the #968 consolidation, plus obs-preflight's one
# binary (#814). Lower it when crates merge binaries; raise it only with a
# reason in the PR.
WORKSPACE_CAP = 22


def test_targets(crate: Path) -> list[str]:
    """Names of the integration-test targets Cargo builds for `crate`."""
    manifest = tomllib.loads((crate / "Cargo.toml").read_text())
    names: dict[str, None] = {}
    explicit_paths = set()
    for entry in manifest.get("test", []):
        name = entry.get("name") or Path(entry.get("path", "")).stem
        names[name] = None
        if "path" in entry:
            explicit_paths.add((crate / entry["path"]).resolve())
    autotests = manifest.get("package", {}).get("autotests", True)
    tests = crate / "tests"
    if autotests and tests.is_dir():
        for path in sorted(tests.glob("*.rs")):
            if path.resolve() not in explicit_paths:
                names.setdefault(path.stem, None)
        for path in sorted(tests.glob("*/main.rs")):
            if path.resolve() not in explicit_paths:
                names.setdefault(path.parent.name, None)
    return list(names)


def workspace_counts(root: Path = ROOT) -> dict[str, int]:
    workspace = tomllib.loads((root / "Cargo.toml").read_text())["workspace"]
    excluded = {(root / e).resolve() for e in workspace.get("exclude", [])}
    counts = {}
    for pattern in workspace.get("members", []):
        for crate in sorted(root.glob(pattern)):
            if crate.resolve() in excluded or not (crate / "Cargo.toml").is_file():
                continue
            counts[crate.name] = len(test_targets(crate))
    return counts


def violations(counts: dict[str, int], per_crate: int = PER_CRATE_CAP, total: int = WORKSPACE_CAP) -> list[str]:
    errors = [
        f"{crate}: {n} integration-test binaries (cap {per_crate}); group tests/*.rs into tests/<category>/main.rs"
        for crate, n in counts.items()
        if n > per_crate
    ]
    if sum(counts.values()) > total:
        errors.append(f"workspace: {sum(counts.values())} integration-test binaries (cap {total})")
    return errors


class BinaryCapTest(unittest.TestCase):
    def test_workspace_within_cap(self):
        errors = violations(workspace_counts())
        self.assertEqual(errors, [], "\n" + "\n".join(errors))


def main(argv: list[str]) -> int:
    counts = workspace_counts()
    if "--report" in argv:
        for crate, n in counts.items():
            print(f"{n:4d}  {crate}")
        print(f"{sum(counts.values()):4d}  total (cap {WORKSPACE_CAP}, per crate {PER_CRATE_CAP})")
    errors = violations(counts)
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
