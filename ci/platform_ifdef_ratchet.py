#!/usr/bin/env -S uv run --script
"""Ratchet for platform conditionals in shared C/C++ files (#847, #848).

    uv run ci/platform_ifdef_ratchet.py            # check against the baseline
    uv run ci/platform_ifdef_ratchet.py --update   # rewrite the baseline

Counts `#if/#ifdef/#ifndef/#elif` lines that name a platform macro in tracked
C/C++/ObjC files under libobs/, frontend/, shared/ and plugins/, skipping
files whose path already marks a platform (obs-nix.c, platform-x11.cpp,
win-capture/, *.m, ...). A file may not exceed its count in
ci/platform-ifdef-baseline.json, and a file absent from it may not gain one:
platform code goes behind the platform facade (frontend/utility/platform.hpp,
libobs's obs-nix.c / obs-windows.c / obs-cocoa.m split) so ci/ci_scope.py can
route platform changes by path alone. Counts may go down; lower the baseline
with --update.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = Path(__file__).resolve().parent / "platform-ifdef-baseline.json"

MACROS = (
    r"WIN32|_WIN32|_WIN64|__linux|_MSC_VER|__APPLE__|__MACH__|TARGET_OS_\w+|__linux__|__FreeBSD__|__OpenBSD__|__unix__"
    r"|OS_WINDOWS|OS_MACOS|OS_LINUX|OS_FREEBSD"
)
CONDITIONAL = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$")
MACRO = re.compile(rf"(?<![A-Za-z0-9_])(?:{MACROS})(?![A-Za-z0-9_])")

SCOPE_DIRS = ("libobs", "frontend", "shared", "plugins")
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".cxx", ".hxx", ".m", ".mm", ".inl")
# Not ours to rewrite: submodules and vendored trees.
EXCLUDED_PREFIXES = ("plugins/obs-websocket/", "plugins/obs-browser/", "deps/")
EXCLUDED_PARTS = ("deps", "vendor", "third_party", "third-party", "librtmp", "rnnoise")

# Same idea as ci/ci_scope.py: a path token that marks a platform.
PLATFORM_TOKENS = frozenset(
    "windows win win32 wasapi dshow d3d9 d3d11 d3d12 dxgi mac macos osx cocoa apple darwin coreaudio avcapture "
    "syphon videotoolbox nix unix linux x11 wayland pipewire pulse pulseaudio alsa v4l2 freebsd posix".split()
)
PLATFORM_SUFFIXES = (".m", ".mm")


def count_platform_conditionals(text: str) -> list[tuple[int, str]]:
    """(1-based line number, stripped line) of each platform conditional."""
    found = []
    for number, line in enumerate(text.splitlines(), 1):
        match = CONDITIONAL.match(line)
        if match and MACRO.search(match.group(1)):
            found.append((number, line.strip()))
    return found


def is_platform_path(path: str) -> bool:
    if path.endswith(PLATFORM_SUFFIXES) or path.rsplit("/", 1)[-1].startswith("platform-"):
        return True
    return any(token in PLATFORM_TOKENS for token in re.split(r"[^a-z0-9]+", path.lower()))


def in_scope(path: str) -> bool:
    parts = path.split("/")
    if len(parts) < 2 or parts[0] not in SCOPE_DIRS or not path.endswith(SOURCE_SUFFIXES):
        return False
    if path.startswith(EXCLUDED_PREFIXES):
        return False
    return not any(part in EXCLUDED_PARTS for part in parts)


def compare(counts: dict[str, int], baseline: dict[str, int]) -> tuple[list[str], list[str]]:
    """(violations, hints): a file above its baseline, or new with a count."""
    bad, hints = [], []
    for path in sorted(counts):
        allowed = baseline.get(path, 0)
        if counts[path] > allowed:
            bad.append(f"{path}: {counts[path]} platform conditionals, baseline {allowed}")
        elif counts[path] < allowed:
            hints.append(f"{path}: {counts[path]} < baseline {allowed}")
    for path in sorted(set(baseline) - set(counts)):
        hints.append(f"{path}: no platform conditionals left, baseline {baseline[path]}")
    return bad, hints


def git_files(root: Path) -> list[str]:
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def scan(root: Path, files: list[str]) -> tuple[dict[str, int], dict[str, list[tuple[int, str]]]]:
    counts, lines = {}, {}
    for path in files:
        if not in_scope(path) or is_platform_path(path):
            continue
        target = root / path
        if not target.is_file():  # submodule gitlink or deleted
            continue
        found = count_platform_conditionals(target.read_text(encoding="utf-8", errors="replace"))
        if found:
            counts[path] = len(found)
            lines[path] = found
    return counts, lines


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--update", action="store_true", help="rewrite the baseline from the tree")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--files", nargs="+", help="check only these paths (tests)")
    args = parser.parse_args(argv)

    counts, lines = scan(args.root, args.files or git_files(args.root))
    if args.update:
        args.baseline.write_text(json.dumps(dict(sorted(counts.items())), indent=2) + "\n")
        print(f"platform_ifdef_ratchet: wrote {len(counts)} files, {sum(counts.values())} conditionals to {args.baseline.name}")
        return 0

    baseline = json.loads(args.baseline.read_text()) if args.baseline.exists() else {}
    bad, hints = compare(counts, baseline)
    if hints:
        print("platform_ifdef_ratchet: fewer conditionals than the baseline, lower it with:")
        print("    uv run ci/platform_ifdef_ratchet.py --update")
        for hint in hints:
            print(f"  {hint}")
    if not bad:
        return 0
    print("platform_ifdef_ratchet: platform conditionals added to a shared file:", file=sys.stderr)
    for message in bad:
        path = message.split(":", 1)[0]
        print(f"  {message}", file=sys.stderr)
        for number, text in lines[path]:
            print(f"    {path}:{number}: {text}", file=sys.stderr)
    print(
        "Move the platform code behind the platform facade (frontend/utility/platform.hpp, "
        "libobs/obs-nix.c|obs-windows.c|obs-cocoa.m, or a platform-named file) and call it "
        "from the shared file. See #847.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
