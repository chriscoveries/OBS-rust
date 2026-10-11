#!/usr/bin/env python3
"""Local gate for obs-rust/obs-studio (zackees/ci.yml GATE-001..010).

One entry point for everything CI's lint and unit-test jobs check, split into
lanes that `ci-lint local-gate run` caches on their inputs and attests in the
commit message (see local-gate.toml and ci-attestations.yml).

    python3 ci/local_gate.py                 # every lane
    python3 ci/local_gate.py --lane lint     # one lane
    python3 ci/local_gate.py --lane lint --fix

Lanes:
  lint         clang-format 22.1.3 and gersemi 0.25.0 over the same files as
               build-aux/run-clang-format and run-gersemi, plus rustfmt and
               clippy -D warnings. Formatters are pinned through uvx, so no
               brew/zsh install is needed.
  rust-test    soldr cargo test --workspace --locked (Tiers 1-3).

Cargo runs with SOLDR_RUSTC_WRAPPER=none (soldr's zccache has replayed stale
rustc output; #134). Set OBS_GATE_ZCCACHE=1 to keep the cache locally.
  tier2-linux  rust/tools/linux-validate/run.sh: libobs + cmocka with
               ENABLE_RUST_LIBOBS OFF and ON in Docker, exported-symbol diff.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLANG_FORMAT = "clang-format==22.1.3"
GERSEMI = "gersemi==0.25.0"

# build-aux/run-clang-format
CLANG_DIRS = ("libobs", "libobs-*", "frontend", "plugins", "deps", "shared", "test")
CLANG_EXTS = (".c", ".cpp", ".h", ".hpp", ".m", ".mm")
CLANG_EXCLUDE = (
    "*/obs-websocket/deps/*",
    "*/decklink/*/decklink-sdk/*",
    "*/mac-syphon/syphon-framework/*",
    "*/libdshowcapture/*",
)
# build-aux/run-gersemi
GERSEMI_DIRS = ("libobs", "libobs-*", "frontend", "plugins", "deps", "shared", "cmake", "test")
GERSEMI_EXCLUDE = (
    "*/jansson/*",
    "*/decklink/*/decklink-sdk/*",
    "*/obs-websocket/*",
    "*/obs-browser/*",
    "*/libdshowcapture/*",
    "cmake/Modules/*",
    "*/legacy.cmake",
)


def run(cmd: list[str], **kw) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True, **kw)


def soldr_cargo(args: list[str]) -> None:
    """Run `soldr cargo ...` with soldr's zccache rustc wrapper disabled.

    The wrapper has replayed stale rustc output (old diagnostics, and green
    results that never compiled the current source; obs-rust/obs-studio#134),
    and the gate's attestation lets CI skip jobs on the strength of these
    lanes. OBS_GATE_ZCCACHE=1 keeps the cached build for local iteration.
    """
    env = dict(os.environ)
    if env.get("OBS_GATE_ZCCACHE") != "1":
        env["SOLDR_RUSTC_WRAPPER"] = "none"
    run(["soldr", "cargo", *args], env=env)


def self_test() -> None:
    """The gate's and the repo layout's unit tests: every ci/test_*.py."""
    run([sys.executable, "-m", "unittest", "discover", "-s", "ci", "-p", "test_*.py"])


def tracked_files() -> list[str]:
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p and (ROOT / p).is_file()]


def top_dir_matches(path: str, dirs: tuple[str, ...]) -> bool:
    top = path.split("/", 1)[0]
    return "/" in path and any(fnmatch.fnmatch(top, d) for d in dirs)


def excluded(path: str, patterns: tuple[str, ...]) -> bool:
    return any(fnmatch.fnmatch(path, p) or fnmatch.fnmatch("/" + path, p) for p in patterns)


def uvx_tool(spec: str, binary: str) -> str:
    """Absolute path of `binary` from the pinned uvx environment for `spec`."""
    code = f"import shutil; print(shutil.which({binary!r}))"
    out = subprocess.run(
        ["uvx", "--from", spec, "python", "-c", code], check=True, capture_output=True, text=True
    ).stdout.strip()
    if not out or out == "None":
        sys.exit(f"local_gate: {binary} not found in uvx environment {spec}")
    return out


def batches(items: list[str], size: int) -> list[list[str]]:
    return [items[i : i + size] for i in range(0, len(items), size)]


def clang_format(files: list[str], fix: bool) -> None:
    sources = [
        f
        for f in files
        if top_dir_matches(f, CLANG_DIRS) and f.endswith(CLANG_EXTS) and not excluded(f, CLANG_EXCLUDE)
    ]
    exe = uvx_tool(CLANG_FORMAT, "clang-format")
    args = ["-i"] if fix else ["--dry-run", "--Werror"]
    print(f"+ clang-format {' '.join(args)} <{len(sources)} files>", flush=True)

    def one(chunk: list[str]) -> subprocess.CompletedProcess:
        return subprocess.run([exe, "-style=file", "-fallback-style=none", *args, *chunk], cwd=ROOT, capture_output=True, text=True)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(one, batches(sources, 50)))
    failed = [r for r in results if r.returncode != 0]
    for r in failed:
        sys.stderr.write(r.stderr)
    if failed:
        sys.exit("local_gate: clang-format check failed (fix with: python3 ci/local_gate.py --lane lint --fix)")


def gersemi(files: list[str], fix: bool) -> None:
    sources = [
        f
        for f in files
        if (f == "CMakeLists.txt" or top_dir_matches(f, GERSEMI_DIRS))
        and (f.endswith("CMakeLists.txt") or f.endswith(".cmake"))
        and not excluded(f, GERSEMI_EXCLUDE)
    ]
    exe = uvx_tool(GERSEMI, "gersemi")
    print(f"+ gersemi {'-i' if fix else '-c'} <{len(sources)} files>", flush=True)
    subprocess.run([exe, "-i" if fix else "-c", "--no-cache", *sources], cwd=ROOT, check=True)


def lane_lint(fix: bool) -> None:
    files = tracked_files()
    clang_format(files, fix)
    gersemi(files, fix)
    self_test()
    soldr_cargo(["fmt", "--all"] + ([] if fix else ["--check"]))
    soldr_cargo(["clippy", "--workspace", "--all-targets", "--locked", "--", "-D", "warnings"])


def lane_rust_test(fix: bool) -> None:
    soldr_cargo(["test", "--workspace", "--locked"])


def lane_tier2_linux(fix: bool) -> None:
    run(["bash", "rust/tools/linux-validate/run.sh"])


LANES = {"lint": lane_lint, "rust-test": lane_rust_test, "tier2-linux": lane_tier2_linux}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lane", choices=sorted(LANES), action="append", help="run only this lane (repeatable)")
    parser.add_argument("--fix", action="store_true", help="lint: rewrite files instead of checking")
    args = parser.parse_args()
    for name in args.lane or list(LANES):
        start = time.monotonic()
        print(f"== lane {name}", flush=True)
        try:
            LANES[name](args.fix)
        except subprocess.CalledProcessError as e:
            print(f"== lane {name}: FAILED ({e.returncode})", flush=True)
            return 1
        print(f"== lane {name}: ok ({time.monotonic() - start:.1f}s)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
