#!/usr/bin/env -S uv run --script
"""Ratchet for platform conditionals in C/C++ files (#847, #848, #1051).

    uv run ci/platform_ifdef_ratchet.py            # check against the baseline
    uv run ci/platform_ifdef_ratchet.py --update   # rewrite the baseline

Scans tracked C/C++/ObjC files under libobs/, frontend/, shared/ and plugins/.
A file's OS comes from its path, through the same classifier ci/ci_scope.py
uses (ci/platform_paths.py): `obs-nix.c`, `platform-x11.cpp`, `win-capture/`,
`*.m`, ... belong to one OS (POSIX names to Linux and macOS); everything else
is shared. Findings per file:

- shared file: every `#if/#ifdef/#ifndef/#elif` that names a host macro, and
  every `#include <...>`/`#import <...>` of an OS header (<windows.h>,
  <AppKit/...>, <X11/...>, <unistd.h>, ...);
- platform file: a host macro or OS header of an OS the file doesn't build
  on (`_WIN32` in obs-cocoa.m), and a conditional that names two OSes
  (`defined(__APPLE__) || defined(__linux__)`).

Comments and string literals are stripped first, and backslash-continued
lines are joined. Compiler and architecture macros (`__GNUC__`,
`__x86_64__`) and portable headers (<pthread.h>, which w32-pthreads
provides on Windows) are not host-specific.

A file may not exceed its count in ci/platform-ifdef-baseline.json, and a
file absent from it may not gain one: platform code goes behind the platform
facade (frontend/utility/platform.hpp, libobs's obs-nix.c / obs-windows.c /
obs-cocoa.m split) so ci/ci_scope.py can route platform changes by path
alone. Counts may go down; lower the baseline with --update. Installed public
headers whose conditionals are part of the API/ABI are listed, with a reason,
in ci/platform-ifdef-exceptions.toml; that list only shrinks.

    rg -n '^\\s*#\\s*(if|ifdef|ifndef|elif)\\b.*\\b__APPLE__\\b' libobs   # by hand
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path

from platform_paths import LINUX, MACOS, WINDOWS, platforms

ROOT = Path(__file__).resolve().parent.parent
BASELINE = Path(__file__).resolve().parent / "platform-ifdef-baseline.json"
EXCEPTIONS = Path(__file__).resolve().parent / "platform-ifdef-exceptions.toml"

# Host macro families, each with the OSes it means. POSIX means Linux and macOS.
_FAMILIES = (
    ("windows", r"WIN32|_WIN32|_WIN64|_MSC_VER|OS_WINDOWS", frozenset({WINDOWS})),
    ("macos", r"__APPLE__|__MACH__|TARGET_OS_\w+|OS_MACOS", frozenset({MACOS})),
    ("linux", r"__linux|__linux__|__FreeBSD__|__OpenBSD__|OS_LINUX|OS_FREEBSD", frozenset({LINUX})),
    ("posix", r"__unix__", frozenset({LINUX, MACOS})),
)
MACROS = "|".join(pattern for _, pattern, _ in _FAMILIES)
FAMILY = [(name, re.compile(rf"(?<![A-Za-z0-9_])(?:{p})(?![A-Za-z0-9_])"), oses) for name, p, oses in _FAMILIES]
CONDITIONAL = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$")
MACRO = re.compile(rf"(?<![A-Za-z0-9_])(?:{MACROS})(?![A-Za-z0-9_])")
# Only <...> includes: a quoted "linux/x.h" is a project path, not an OS header.
INCLUDE = re.compile(r"^\s*#\s*(?:include|import)\s*<([^>]+)>")
HEADERS = (
    (
        re.compile(
            r"(?:windows|winsock2|ws2tcpip|objbase|shlobj|mmdeviceapi|audioclient|psapi|dbghelp)\.h|d3d\w*\.h|dxgi\w*\.h",
            re.I,
        ),
        frozenset({WINDOWS}),
    ),
    (
        re.compile(r"(?:CoreFoundation|CoreAudio|AudioToolbox|AppKit|Cocoa|IOKit|mach|mach-o|objc)/.*|TargetConditionals\.h"),
        frozenset({MACOS}),
    ),
    (
        re.compile(r"(?:X11|xcb|linux|pulse|pipewire|spa|dbus|gio)/.*|wayland-.*|sys/(?:epoll|inotify)\.h"),
        frozenset({LINUX}),
    ),
    (re.compile(r"(?:unistd|dlfcn|poll)\.h|sys/(?:mman|socket|wait)\.h"), frozenset({LINUX, MACOS})),
)
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
RAW_STRING = re.compile(r'(?:u8|u|U|L)?R"([^()\\\s]{0,16})\(')

SCOPE_DIRS = ("libobs", "frontend", "shared", "plugins")
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".cxx", ".hxx", ".m", ".mm", ".inl")
HEADER_SUFFIXES = (".h", ".hpp", ".hh", ".hxx", ".inl")
# Implementation files (BUILD.md): sources, though they end in .h/.hpp.
IMPL_SUFFIXES = (".c.h", ".cpp.hpp", ".m.h", ".mm.hpp")
# Not ours to rewrite: submodules and vendored trees.
# shared/obs-d3d8-api/ is a vendored Microsoft header (#886); decklink-sdk/ is
# the vendored Blackmagic SDK.
EXCLUDED_PREFIXES = ("plugins/obs-websocket/", "plugins/obs-browser/", "deps/", "shared/obs-d3d8-api/")
EXCLUDED_PARTS = ("deps", "vendor", "third_party", "third-party", "librtmp", "rnnoise", "decklink-sdk")


def strip_comments(text: str) -> str:
    """`text` without comments or raw-string bodies, newlines kept so line
    numbers hold. String and char literals are kept but never open a comment."""
    text = text.replace("\r\n", "\n")
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i : i + 2]
        if two == "//":
            # A backslash-newline continues a line comment.
            while i < n and text[i] != "\n":
                if text[i] == "\\" and text[i + 1 : i + 2] == "\n":
                    out.append("\n")
                    i += 1
                i += 1
            continue
        if two == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(" " + "\n" * text.count("\n", i, end))
            i = end
            continue
        if c in "uULR" and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            if m := RAW_STRING.match(text, i):
                close = text.find(f"){m.group(1)}\"", m.end())
                end = n if close < 0 else close + len(m.group(1)) + 2
                out.append('""' + "\n" * text.count("\n", i, end))
                i = end
                continue
        if c == "'" and i > 0 and text[i - 1].isdigit() and text[i + 1 : i + 2].isalnum():
            out.append(c)  # C++14 digit separator, not a char literal
            i += 1
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] not in (c, "\n"):
                j += 2 if text[j] == "\\" and text[j + 1 : j + 2] != "\n" else 1
            end = j + 1 if j < n and text[j] == c else j
            out.append(text[i:end])
            i = end
            continue
        out.append(c)
        i += 1
    return "".join(out)


def directives(text: str) -> list[tuple[int, str]]:
    """(1-based first line, joined text) of each preprocessor line."""
    lines = strip_comments(text).split("\n")
    found = []
    i = 0
    while i < len(lines):
        start, line = i, lines[i]
        while line.endswith("\\") and i + 1 < len(lines):
            i += 1
            line = line[:-1] + " " + lines[i]
        i += 1
        if line.lstrip().startswith("#"):
            found.append((start + 1, " ".join(line.split())))
    return found


def named_families(expr: str) -> list[tuple[str, frozenset[str]]]:
    expr = STRING.sub('""', expr)
    return [(name, oses) for name, pattern, oses in FAMILY if pattern.search(expr)]


def header_oses(header: str) -> frozenset[str] | None:
    for pattern, oses in HEADERS:
        if pattern.fullmatch(header.strip()):
            return oses
    return None


def count_platform_conditionals(text: str) -> list[tuple[int, str]]:
    """(1-based line number, line) of each conditional naming a host macro."""
    found = []
    for number, line in directives(text):
        match = CONDITIONAL.match(line)
        if match and named_families(match.group(1)):
            found.append((number, line))
    return found


def findings(path: str, text: str) -> list[tuple[int, str, str]]:
    """(line, text, why) of each host-boundary finding in `path`."""
    own = platforms(path)
    where = "a shared file" if own is None else f"a {'/'.join(sorted(own))} file"
    found = []
    for number, line in directives(text):
        if match := CONDITIONAL.match(line):
            families = named_families(match.group(1))
            if not families:
                continue
            if own is None:
                found.append((number, line, "host conditional in a shared file"))
                continue
            wrong = sorted({name for name, oses in families if not oses & own})
            single = {name for name, oses in families if len(oses) == 1}
            if wrong:
                found.append((number, line, f"names {', '.join(wrong)} in {where}"))
            elif len(single) > 1:
                found.append((number, line, f"names {' and '.join(sorted(single))} in one conditional"))
        elif match := INCLUDE.match(line):
            oses = header_oses(match.group(1))
            if oses is not None and (own is None or not oses & own):
                found.append((number, line, f"{'/'.join(sorted(oses))} header in {where}"))
    return found


def is_platform_path(path: str) -> bool:
    return platforms(path) is not None


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
            bad.append(f"{path}: {counts[path]} platform findings, baseline {allowed}")
        elif counts[path] < allowed:
            hints.append(f"{path}: {counts[path]} < baseline {allowed}")
    for path in sorted(set(baseline) - set(counts)):
        hints.append(f"{path}: no platform findings left, baseline {baseline[path]}")
    return bad, hints


def git_files(root: Path) -> list[str]:
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def read(root: Path, path: str) -> str | None:
    target = root / path
    if not target.is_file():  # submodule gitlink or deleted
        return None
    return target.read_text(encoding="utf-8", errors="replace")


def load_exceptions(root: Path, file: Path) -> tuple[set[str], list[str]]:
    """(exempt paths, errors). An exception is an existing header, with a
    reason, that still has findings."""
    if not file.exists():
        return set(), []
    paths, errors = set(), []
    for entry in tomllib.loads(file.read_text()).get("exception", []):
        path = entry.get("path", "")
        if not path.endswith(HEADER_SUFFIXES) or path.endswith(IMPL_SUFFIXES):
            errors.append(f"{path}: not a header; only public headers get exceptions, move the code behind the facade")
        elif not str(entry.get("reason", "")).strip():
            errors.append(f"{path}: an exception needs a reason")
        elif (text := read(root, path)) is None:
            errors.append(f"{path}: missing; remove its exception")
        elif not findings(path, text):
            errors.append(f"{path}: no platform findings left; remove its exception")
        paths.add(path)
    return paths, errors


def scan(root: Path, files: list[str], exempt: set[str] = frozenset()) -> tuple[dict[str, int], dict[str, list[tuple[int, str, str]]]]:
    counts, lines = {}, {}
    for path in files:
        if not in_scope(path) or path in exempt:
            continue
        text = read(root, path)
        if text is None:
            continue
        found = findings(path, text)
        if found:
            counts[path] = len(found)
            lines[path] = found
    return counts, lines


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--update", action="store_true", help="rewrite the baseline from the tree")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--exceptions", type=Path, default=EXCEPTIONS)
    parser.add_argument("--files", nargs="+", help="check only these paths (tests)")
    args = parser.parse_args(argv)

    exempt, errors = load_exceptions(args.root, args.exceptions)
    if errors:
        print(f"platform_ifdef_ratchet: bad entries in {args.exceptions.name}:", file=sys.stderr)
        for message in errors:
            print(f"  {message}", file=sys.stderr)
        return 1
    counts, lines = scan(args.root, args.files or git_files(args.root), exempt)
    if args.update:
        args.baseline.write_text(json.dumps(dict(sorted(counts.items())), indent=2) + "\n")
        print(f"platform_ifdef_ratchet: wrote {len(counts)} files, {sum(counts.values())} findings to {args.baseline.name}")
        return 0

    baseline = json.loads(args.baseline.read_text()) if args.baseline.exists() else {}
    bad, hints = compare(counts, baseline)
    if hints:
        print("platform_ifdef_ratchet: fewer findings than the baseline, lower it with:")
        print("    uv run ci/platform_ifdef_ratchet.py --update")
        for hint in hints:
            print(f"  {hint}")
    if not bad:
        return 0
    print("platform_ifdef_ratchet: new platform findings:", file=sys.stderr)
    for message in bad:
        path = message.split(":", 1)[0]
        print(f"  {message}", file=sys.stderr)
        for number, text, why in lines[path]:
            print(f"    {path}:{number}: {text}  ({why})", file=sys.stderr)
    print(
        "Move the platform code behind the platform facade (frontend/utility/platform.hpp, "
        "libobs/obs-nix.c|obs-windows.c|obs-cocoa.m, or a platform-named file) and call it "
        "from the shared file; a platform file names only its own OS. See #847 and #1051.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
