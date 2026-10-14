#!/usr/bin/env -S uv run --script
"""List GCC/Clang `-W` options that CMake would pass to MSVC (obs-rust/obs-studio#634).

    uv run ci/msvc_flags.py        # prints path:line: flag, exits 1 if any

MSVC rejects unknown `-W...` options (D8021), so the Windows build failed
late on flags that GCC and Clang accept. A `-W` option given to
target_compile_options, add_compile_options or a COMPILE_OPTIONS property
must be inside a compiler-id generator expression
(`$<$<C_COMPILER_ID:GNU,Clang>:-Wno-x>`), an if() branch that excludes MSVC,
or a file that never builds on Windows. Options kept in variables are not
followed.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Trees we don't own, and files or plugins that never build on Windows.
SKIP_PREFIXES = (
    "deps/",
    "plugins/obs-websocket/",
    "plugins/obs-browser/",
    "cmake/macos/",
    "cmake/linux/",
    "plugins/mac-",
    "plugins/linux-",
    "plugins/sndio/",
)
# os-macos.cmake, os-linux.cmake, os-freebsd.cmake, ...: included per platform.
PLATFORM_FILE = re.compile(r"^os-(macos|linux|freebsd|openbsd|posix)\.cmake$")
CALLS = {"target_compile_options", "add_compile_options"}
PROPERTY_CALLS = {"set_source_files_properties", "set_target_properties", "set_property"}
WINDOWS = re.compile(r"\b(OS_WINDOWS|WIN32|MSVC)\b|\"MSVC\"")
NOT_WINDOWS = re.compile(r"\bNOT\s+(OS_WINDOWS|WIN32|MSVC)\b")
OTHER_PLATFORM = re.compile(
    r"\b(OS_LINUX|OS_MACOS|OS_FREEBSD|OS_OPENBSD|OS_POSIX|APPLE|UNIX)\b|\"(GNU|Clang|AppleClang)\""
)
FLAG = re.compile(r"(?<![\w-])-W[A-Za-z][\w=+.-]*")


def excludes_msvc(cond: str) -> bool:
    """True when the branch guarded by `cond` never runs with MSVC."""
    if NOT_WINDOWS.search(cond):
        return True
    return bool(OTHER_PLATFORM.search(cond)) and not WINDOWS.search(cond)


def covers_windows(cond: str) -> bool:
    """True when `cond` holds for every Windows/MSVC build, so its else() excludes MSVC."""
    return bool(WINDOWS.search(cond)) and not NOT_WINDOWS.search(cond) and " AND " not in cond.upper()


def strip_comments(text: str) -> str:
    out = []
    for line in text.split("\n"):
        in_quote = False
        for i, ch in enumerate(line):
            if ch == '"' and (i == 0 or line[i - 1] != "\\"):
                in_quote = not in_quote
            elif ch == "#" and not in_quote:
                line = line[:i]
                break
        out.append(line)
    return "\n".join(out)


def commands(text: str):
    """Yield (line, name, args) for each `name(args)` command."""
    text = strip_comments(text)
    pos = 0
    pattern = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    while True:
        m = pattern.search(text, pos)
        if not m:
            return
        depth, i, in_quote = 1, m.end(), False
        while i < len(text) and depth:
            ch = text[i]
            if ch == '"' and text[i - 1] != "\\":
                in_quote = not in_quote
            elif not in_quote:
                depth += {"(": 1, ")": -1}.get(ch, 0)
            i += 1
        yield text.count("\n", 0, m.start()) + 1, m.group(1).lower(), text[m.end() : i - 1]
        pos = i


def strip_genex(args: str) -> str:
    """Remove $<...> generator expressions, nested ones included."""
    out, depth, i = [], 0, 0
    while i < len(args):
        if args.startswith("$<", i):
            depth += 1
            i += 2
            continue
        if depth and args[i] == ">":
            depth -= 1
        elif not depth:
            out.append(args[i])
        i += 1
    return "".join(out)


def unguarded_flags(path: str, text: str) -> list[tuple[int, str]]:
    if path.startswith(SKIP_PREFIXES) or PLATFORM_FILE.search(path.rsplit("/", 1)[-1]):
        return []
    found = []
    # One frame per open if(): [this branch excludes MSVC, an earlier branch covers Windows].
    stack: list[list[bool]] = []
    for line, name, args in commands(text):
        if name == "if":
            stack.append([excludes_msvc(args), covers_windows(args)])
        elif name == "elseif" and stack:
            stack[-1] = [excludes_msvc(args), stack[-1][1] or covers_windows(args)]
        elif name == "else" and stack:
            stack[-1] = [stack[-1][1], False]
        elif name == "endif" and stack:
            stack.pop()
        elif any(frame[0] for frame in stack):
            continue
        elif name in CALLS or (name in PROPERTY_CALLS and "COMPILE_OPTIONS" in args):
            options = args.split("COMPILE_OPTIONS", 1)[1] if name in PROPERTY_CALLS else args
            found.extend((line, flag) for flag in FLAG.findall(strip_genex(options)))
    return found


def scan(root: Path = ROOT) -> list[str]:
    files = subprocess.run(
        ["git", "ls-files", "*CMakeLists.txt", "*.cmake"], cwd=root, capture_output=True, text=True, check=True
    ).stdout.split()
    hits = []
    for name in files:
        path = root / name
        if path.is_file():
            hits.extend(f"{name}:{line}: {flag}" for line, flag in unguarded_flags(name, path.read_text(errors="replace")))
    return hits


if __name__ == "__main__":
    hits = scan()
    print("\n".join(hits))
    sys.exit(1 if hits else 0)
