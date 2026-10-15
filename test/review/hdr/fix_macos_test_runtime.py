#!/usr/bin/env -S uv run --script
"""Repair local test executables only; never sign the OBS application or plugins."""
from __future__ import annotations
import argparse
import json
import plistlib
import re
import subprocess
from pathlib import Path


def candidates(spec: dict, build: Path) -> list[Path]:
    found = set()
    build = build.resolve()
    for test in spec.get("tests", []):
        for argument in test.get("command", []):
            path = Path(argument)
            if not path.is_absolute():
                continue
            path = path.resolve()
            if path.is_relative_to(build) and path.name.startswith("test_") and path.suffix != ".dylib":
                if not path.is_file():
                    raise RuntimeError(f"Registered test executable is missing: {path}")
                found.add(path)
    return sorted(found)


def rpaths(output: str) -> set[str]:
    return set(re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", output))


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("build", "framework-dir", "deps-lib", "cmocka-lib", "logs"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    if run("uname", "-s") != "Darwin":
        parser.error("This operation is restricted to local macOS test executables")
    build = args.build.resolve()
    if not (build / "CMakeCache.txt").is_file():
        parser.error("Expected a configured CMake build directory")
    libraries = [args.framework_dir.resolve(), args.deps_lib.resolve(), args.cmocka_lib.resolve()]
    if any(not path.is_dir() for path in libraries):
        parser.error("A required runtime-library directory is missing")
    spec = json.loads(run("ctest", "--test-dir", str(build), "-C", "RelWithDebInfo", "--show-only=json-v1"))
    args.logs.mkdir(parents=True, exist_ok=True)
    entitlement = args.logs / "test-runtime-entitlements.plist"
    entitlement.write_bytes(plistlib.dumps({"com.apple.security.cs.disable-library-validation": True,
                                          "com.apple.security.cs.allow-dyld-environment-variables": True}))
    touched = []
    for exe in candidates(spec, build):
        description = run("file", "-b", str(exe))
        if "Mach-O" not in description or "executable" not in description:
            continue
        existing = rpaths(run("otool", "-l", str(exe)))
        for path in dict.fromkeys([exe.parent, *libraries]):
            if str(path) not in existing:
                subprocess.run(["install_name_tool", "-add_rpath", str(path), str(exe)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", "--options", "runtime", "--entitlements",
                        str(entitlement), str(exe)], check=True)
        subprocess.run(["codesign", "--verify", "--strict", str(exe)], check=True)
        touched.append(str(exe))
    if not touched:
        raise SystemExit("REFUSED: no registered Mach-O test executables found")
    (args.logs / "test-runtime-repairs.json").write_text(json.dumps(touched, indent=2) + "\n")
    print(f"Repaired {len(touched)} registered test executables. App/plugin signing unchanged.")


if __name__ == "__main__":
    main()
