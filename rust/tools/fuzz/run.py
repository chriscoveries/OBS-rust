#!/usr/bin/env -S uv run --script
"""Run every differential fuzz target with persistent corpora (#144)."""
from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import json
import subprocess
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FUZZ = ROOT / "rust/obs-codec/fuzz"


def positive(value: str) -> int:
    result = int(value)
    if result < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--seconds", type=positive, default=3600, help="time per target")
    parser.add_argument("--toolchain", default="nightly-2026-10-14")
    parser.add_argument("--target", action="append", help="repeat to select targets; default: all")
    args = parser.parse_args()
    targets = [item["name"] for item in tomllib.loads((FUZZ / "Cargo.toml").read_text())["bin"]]
    if args.target:
        if len(set(args.target)) != len(args.target) or set(args.target) - set(targets):
            parser.error("targets must be distinct declared fuzz binaries")
        targets = args.target
    state = args.state_dir.expanduser().resolve()
    state.mkdir(parents=True, exist_ok=True)
    # Linux cron runner: an OS lock releases on exit or crash; no stale PID file.
    with (state / "run.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("another fuzz run owns this state directory", flush=True)
            return 75
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        logs = state / "runs" / stamp
        logs.mkdir(parents=True)
        prefix = ["soldr", "cargo", f"+{args.toolchain}", "fuzz"]
        def capture(*command: str) -> str:
            return subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                  check=False).stdout.strip()

        report = {"started": stamp, "head": capture("git", "rev-parse", "HEAD"),
            # Uncommitted edits change what was fuzzed; record them.
            "dirty": capture("git", "status", "--porcelain").splitlines(),
            "toolchain": args.toolchain,
            "rustc": capture("soldr", "rustup", "run", args.toolchain, "rustc", "-V"),
            "cargo_fuzz": capture("soldr", "cargo", f"+{args.toolchain}", "fuzz", "--version"),
            "seconds_per_target": args.seconds,
            "targets": [], "build_returncode": None}
        with (logs / "build.log").open("w") as output:
            result = subprocess.run(prefix + ["build"], cwd=FUZZ.parent,
                                    stdout=output, stderr=subprocess.STDOUT, check=False)
        report["build_returncode"] = result.returncode
        if result.returncode == 0:
            for target in targets:
                corpus = state / "corpus" / target
                artifacts = state / "artifacts" / target
                corpus.mkdir(parents=True, exist_ok=True)
                artifacts.mkdir(parents=True, exist_ok=True)
                command = prefix + ["run", target, str(corpus), "--",
                                    f"-max_total_time={args.seconds}", "-max_len=4096",
                                    "-timeout=30", "-rss_limit_mb=2048",
                                    f"-artifact_prefix={artifacts}/"]
                print(f"fuzz: {target} started ({args.seconds}s)", flush=True)
                with (logs / f"{target}.log").open("w") as output:
                    result = subprocess.run(command, cwd=FUZZ.parent, stdout=output,
                                            stderr=subprocess.STDOUT, check=False)
                report["targets"].append({"name": target, "returncode": result.returncode,
                                          "command": command, "corpus": str(corpus)})
                print(f"fuzz: {target} exited {result.returncode}", flush=True)
        report["finished"] = dt.datetime.now(dt.timezone.utc).isoformat()
        (logs / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"fuzz: evidence {logs}", flush=True)
        return 0 if (report["build_returncode"] == 0
                     and all(item["returncode"] == 0 for item in report["targets"])) else 1


if __name__ == "__main__":
    raise SystemExit(main())
