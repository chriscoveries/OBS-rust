#!/usr/bin/env -S uv run --script
"""Run the existing, configured OFF/ON colour-dictionary test builds."""
from __future__ import annotations
import json
import plistlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TARGET = "test_vt_colour_dictionary"


def run(args: list[str], log: Path) -> str:
    result = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with log.open("a", encoding="utf-8") as stream:
        stream.write("$ " + " ".join(args) + "\n" + result.stdout)
    print(result.stdout, end="")
    result.check_returncode()
    return result.stdout


def cache_value(cache: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}:[^=]*=(.*)$", cache, re.M)
    if not match:
        raise RuntimeError(f"missing cache setting: {name}")
    return match.group(1)


def main() -> None:
    if sys.platform != "darwin" or len(sys.argv) != 3:
        raise SystemExit("usage on macOS: uv run test/review/vt-colour-dictionary/run-macos.py OFF_BUILD ON_BUILD")
    logs = Path(tempfile.mkdtemp(prefix="obs-vt-colour-"))
    print(f"Logs: {logs}")
    entitlements = logs / "test-entitlements.plist"
    entitlements.write_bytes(plistlib.dumps({"com.apple.security.cs.disable-library-validation": True}))
    for mode, argument in zip(("OFF", "ON"), sys.argv[1:], strict=True):
        build = Path(argument).resolve(strict=True)
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
        if cache_value(cache, "ENABLE_RUST_LIBOBS") != mode:
            raise RuntimeError(f"expected ENABLE_RUST_LIBOBS={mode}")
        if Path(cache_value(cache, "CMAKE_HOME_DIRECTORY")).resolve() != ROOT:
            raise RuntimeError("build belongs to another source checkout")
        log = logs / f"{mode}.log"
        run(["git", "rev-parse", "HEAD"], log)
        run(["git", "status", "--porcelain"], log)
        run(["cmake", "--build", str(build), "--config", "RelWithDebInfo", "--parallel", "2", "--target", "libobs", TARGET], log)
        manifest = json.loads(run(["ctest", "--test-dir", str(build), "-C", "RelWithDebInfo", "--show-only=json-v1", "-R", f"^{TARGET}$"], log))
        tests = manifest.get("tests", [])
        if len(tests) != 1 or tests[0]["name"] != TARGET:
            raise RuntimeError("expected exactly one registered colour-dictionary test")
        executable = Path(tests[0]["command"][0]).resolve(strict=True)
        if not executable.is_relative_to(build) or executable.name != TARGET:
            raise RuntimeError("refusing to alter an unexpected test executable")
        frameworks = sorted(build.rglob("libobs.framework"))
        if not frameworks:
            raise RuntimeError("libobs.framework missing from build")
        rpaths = {executable.parent, frameworks[0].parent.resolve()}
        rpaths.update(p.parent.resolve() for p in (ROOT / ".deps").rglob("libavcodec*.dylib"))
        if len(rpaths) < 3:
            raise RuntimeError("obs-deps library directory not found")
        load_commands = run(["otool", "-l", str(executable)], log)
        existing = set(re.findall(r"\bpath (.*?) \(offset \d+\)", load_commands))
        for path in sorted(rpaths):
            if str(path) not in existing:
                run(["install_name_tool", "-add_rpath", str(path), str(executable)], log)
        run(["codesign", "--force", "--sign", "-", "--entitlements", str(entitlements), str(executable)], log)
        run(["ctest", "--test-dir", str(build), "-C", "RelWithDebInfo", "--output-on-failure", "--repeat", "until-fail:10", "-R", f"^{TARGET}$"], log)


if __name__ == "__main__":
    main()
