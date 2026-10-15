# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and run the standalone before/after scaler benchmark."""

from __future__ import annotations

import argparse
import json
import platform
import re
import subprocess
import sys
import tempfile
import threading
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BENCH = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "ab_bench"))
from core import (
    interleaved_schedule,
    machine_metadata,
    markdown_report,
    sample_load,
    summarize_rounds,
)

# These are the immutable source revisions compared by upstream draft #743.
BASE_SHA = "a48a9a13d80d3ac5ded749093e6ac2a08855e189"
HEAD_SHA = "77db966d12234b360166f6cf059b55d7bf4e9537"
SOURCE_PATH = "libobs/media-io/video-scaler-ffmpeg.c"
LOAD_LIMIT = 15.0


def run(
    command: list[str], *, cwd: Path = ROOT, capture: bool = False
) -> subprocess.CompletedProcess:
    result = subprocess.run(
        command, check=False, cwd=cwd, text=True, capture_output=capture
    )
    if result.returncode:
        if capture:
            detail = result.stderr or result.stdout
            if detail:
                print(_redact_paths(detail), file=sys.stderr, end="")
        rendered = _redact_paths(" ".join(command))
        raise RuntimeError(
            f"command failed with exit code {result.returncode}: {rendered}"
        )
    return result


def _redact_paths(text: str) -> str:
    text = text.replace(str(ROOT), "<checkout>")
    return text.replace(tempfile.gettempdir(), "<temporary-path>")


def source_at(revision: str, destination: Path) -> None:
    result = run(["git", "show", f"{revision}:{SOURCE_PATH}"], capture=True)
    destination.write_text(result.stdout, encoding="utf-8")


def ffmpeg_version() -> str:
    versions = []
    for library in ("libswscale", "libavutil"):
        result = run(["pkg-config", "--modversion", library], capture=True)
        versions.append(f"{library} {result.stdout.strip()}")
    return ", ".join(versions)


def configure_and_build(
    build_dir: Path, base_source: Path, head_source: Path
) -> tuple[Path, str]:
    run(
        [
            "cmake",
            "-S",
            str(BENCH),
            "-B",
            str(build_dir),
            f"-DOBS_SOURCE_ROOT={ROOT}",
            f"-DSCALER_BASE_SOURCE={base_source}",
            f"-DSCALER_HEAD_SOURCE={head_source}",
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        capture=True,
    )
    run(["cmake", "--build", str(build_dir), "--parallel", "2"], capture=True)
    cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8")
    compiler_path = _cache_value(cache, "CMAKE_C_COMPILER")
    compiler = "C compiler unknown version"
    if compiler_path:
        version = run([compiler_path, "--version"], capture=True).stdout.splitlines()
        if version:
            compiler = version[0].strip()
    print("Standalone scaler source snapshots configured and built successfully.")
    executable = build_dir / "obs-scaler-bench"
    if platform.system() == "Windows":
        executable = executable.with_suffix(".exe")
    return executable, compiler


def _cache_value(cache: str, key: str) -> str | None:
    match = re.search(rf"^{re.escape(key)}:[^=]*=(.*)$", cache, re.MULTILINE)
    return match.group(1).strip() if match else None


def run_unit_tests() -> None:
    run(
        [
            sys.executable,
            "-m",
            "unittest",
            "discover",
            "-s",
            str(ROOT / "tools" / "ab_bench"),
            "-p",
            "test_*.py",
            "-v",
        ]
    )


def parse_bench_output(text: str, mode: str, rounds: int) -> list[dict]:
    records = [json.loads(line) for line in text.splitlines() if line.strip()]
    correctness = [record for record in records if record.get("kind") == "correctness"]
    if len(correctness) != 12 or any(
        record.get("equal") is not True for record in correctness
    ):
        raise ValueError(
            "benchmark did not pass all 12 byte-for-byte correctness cases"
        )
    cases = {record["case"] for record in correctness}
    if len(cases) != 12:
        raise ValueError("benchmark did not report 12 distinct scaler cases")
    if mode == "quick":
        if len(records) != 12:
            raise ValueError("quick mode must contain correctness records only")
        return records
    timings = [record for record in records if record.get("kind") == "round"]
    if len(timings) != 12 * rounds:
        raise ValueError("benchmark returned an incomplete timing matrix")
    for case in cases:
        samples = sorted(
            (row for row in timings if row["case"] == case),
            key=lambda row: row["round"],
        )
        if len(samples) != rounds or [row["round"] for row in samples] != list(
            range(rounds)
        ):
            raise ValueError(f"incomplete or duplicate rounds for {case}")
        if [row["order"] for row in samples] != interleaved_schedule(rounds):
            raise ValueError(f"before/after ordering did not alternate for {case}")
        for row in samples:
            if row["frames"] <= 0 or row["before_ns"] <= 0 or row["after_ns"] <= 0:
                raise ValueError(f"invalid timing sample for {case}: {row!r}")
    return records


def format_load(sample: dict | None) -> str:
    if not sample:
        return "not sampled (dry/quick path)"
    load = f"{sample['busy_percent']:.1f}% busy via {sample['method']} over {sample['seconds']:.1f}s"
    if "initial_busy_percent" in sample:
        load += f" (initially {sample['initial_busy_percent']:.1f}%)"
    elif sample.get("forced"):
        load += " (forced)"
    return load


def collect_during_load(result: dict, errors: list[Exception]) -> None:
    try:
        sample = sample_load(3)
        result.update(
            busy_percent=sample.busy_percent,
            method=sample.method,
            seconds=sample.seconds,
        )
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        errors.append(error)


def confirm_quiet_machine(force: bool) -> dict:
    first = sample_load(3)
    if first.busy_percent <= LOAD_LIMIT or force:
        return {
            "busy_percent": first.busy_percent,
            "method": first.method,
            "seconds": first.seconds,
            "forced": force,
        }
    print(
        f"Warning: whole-machine CPU load is {first.busy_percent:.1f}% "
        f"(limit {LOAD_LIMIT:.0f}%). Close busy applications before measuring.",
        file=sys.stderr,
    )
    if not sys.stdin.isatty():
        raise RuntimeError(
            "use --force to continue non-interactively, or rerun on a quieter machine"
        )
    input(
        "After closing applications, press Enter to sample load once more; Ctrl-C cancels: "
    )
    second = sample_load(3)
    if second.busy_percent > LOAD_LIMIT:
        raise RuntimeError(
            f"CPU load remains {second.busy_percent:.1f}%; rerun with --force only if you accept the noisy host"
        )
    return {
        "busy_percent": second.busy_percent,
        "method": second.method,
        "seconds": second.seconds,
        "forced": False,
        "initial_busy_percent": first.busy_percent,
    }


def validate_post_url(url: str) -> None:
    if not re.fullmatch(
        r"https://github\.com/chriscoveries/obsrust-fork/pull/[0-9]+", url
    ):
        raise ValueError(
            "--post accepts only a chriscoveries/obsrust-fork pull request URL"
        )
    run(["gh", "auth", "status", "--hostname", "github.com"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument(
        "--quick",
        action="store_true",
        help="correctness only; no load sample or timing",
    )
    modes.add_argument(
        "--dry-timing",
        action="store_true",
        help="one tiny smoke round; never a measurement",
    )
    modes.add_argument(
        "--measure",
        action="store_true",
        help="run full interleaved reviewer measurements",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="continue a full measurement on a busy machine",
    )
    parser.add_argument(
        "--frames",
        type=int,
        default=30,
        help="frames in each timed batch (default: 30)",
    )
    parser.add_argument(
        "--rounds", type=int, default=7, help="interleaved pairs per case (minimum 7)"
    )
    parser.add_argument(
        "--output-dir", type=Path, default=Path.cwd() / "scaler-bench-results"
    )
    parser.add_argument(
        "--post",
        metavar="PR_URL",
        help="post the report to this fork PR after a full measurement",
    )
    args = parser.parse_args()
    mode = "measure" if args.measure else "dry-timing" if args.dry_timing else "quick"
    if args.post and mode != "measure":
        parser.error("--post is available only with --measure")
    if mode == "measure" and args.rounds < 7:
        parser.error("--rounds must be at least 7")
    if not 1 <= args.frames <= 1000 or not 7 <= args.rounds <= 1000:
        parser.error("--frames must be 1..1000 and --rounds must be 7..1000")
    if args.force and mode != "measure":
        parser.error("--force applies only to --measure")
    if args.post:
        validate_post_url(args.post)

    run_unit_tests()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    report_path = args.output_dir / f"scaler-{mode}-{stamp}.md"
    raw_path = args.output_dir / f"scaler-{mode}-{stamp}.json"

    with tempfile.TemporaryDirectory(prefix="obs-scaler-ab-") as scratch:
        temp = Path(scratch)
        base_source, head_source = temp / "before.c", temp / "after.c"
        source_at(BASE_SHA, base_source)
        source_at(HEAD_SHA, head_source)
        executable, compiler = configure_and_build(
            temp / "build", base_source, head_source
        )
        ffmpeg = ffmpeg_version()
        metadata = machine_metadata(compiler, ffmpeg)
        load_before = confirm_quiet_machine(args.force) if mode == "measure" else None
        command = [str(executable), f"--{mode}"]
        if mode == "measure":
            schedule = ",".join(interleaved_schedule(args.rounds))
            command.extend(
                [
                    "--frames",
                    str(args.frames),
                    "--rounds",
                    str(args.rounds),
                    "--schedule",
                    schedule,
                ]
            )
        elif mode == "dry-timing":
            command.extend(["--schedule", "AB"])
        if mode == "measure":
            during: dict = {}
            sampler_errors: list[Exception] = []
            sampler = threading.Thread(
                target=collect_during_load, args=(during, sampler_errors), daemon=True
            )
            sampler.start()
            result = run(command, cwd=ROOT, capture=True)
            sampler.join()
            if sampler_errors:
                raise RuntimeError(
                    f"during-run CPU sampler failed: {sampler_errors[0]}"
                )
            load_during = during
        else:
            result = run(command, cwd=ROOT, capture=True)
            load_during = None
        records = parse_bench_output(
            result.stdout, mode, 1 if mode == "dry-timing" else args.rounds
        )

    report_cases: list[dict]
    if mode == "dry-timing":
        round_rows = [row for row in records if row["kind"] == "round"]
        report_cases = [
            {
                "case": row["case"],
                "smoke_before_ns": row["before_ns"] // row["frames"],
                "smoke_after_ns": row["after_ns"] // row["frames"],
            }
            for row in round_rows
        ]
        report = markdown_report(
            metadata=metadata,
            load_before=format_load(None),
            load_during=format_load(None),
            base_sha=BASE_SHA,
            head_sha=HEAD_SHA,
            cases=report_cases,
            not_a_measurement=True,
        )
    elif mode == "measure":
        round_rows = [row for row in records if row["kind"] == "round"]
        report_cases = summarize_rounds(round_rows)
        report = markdown_report(
            metadata=metadata,
            load_before=format_load(load_before),
            load_during=format_load(load_during),
            base_sha=BASE_SHA,
            head_sha=HEAD_SHA,
            cases=report_cases,
        )
    else:
        report_cases = []
        report = ""

    raw = {
        "schema_version": 1,
        "mode": mode,
        "base_sha": BASE_SHA,
        "head_sha": HEAD_SHA,
        "metadata": metadata,
        "load": {"before": load_before, "during": load_during},
        "records": records,
        "summary": report_cases,
    }
    raw_path.write_text(
        json.dumps(raw, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )
    if report:
        report_path.write_text(report, encoding="utf-8")
        print(report)
    else:
        print("Correctness: 12/12 before/after cases produced byte-identical output.")
    print(f"Raw per-round JSON saved: {raw_path.name}")
    if report:
        print(f"Markdown report saved: {report_path.name}")
    if args.post:
        run(["gh", "pr", "comment", args.post, "--body-file", str(report_path)])
        print(f"Posted report to {args.post}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"scaler benchmark failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
