# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared statistics, load parsing, scheduling, and Markdown for A/B benches."""

from __future__ import annotations

import platform
import random
import re
import subprocess
import time
from collections.abc import Iterable, Sequence
from dataclasses import dataclass


@dataclass(frozen=True)
class LoadSample:
    busy_percent: float
    method: str
    seconds: float


def percentile(values: Sequence[float], fraction: float) -> float:
    if not values:
        raise ValueError("percentile requires at least one value")
    if not 0 <= fraction <= 1:
        raise ValueError("fraction must be between zero and one")
    ordered = sorted(float(value) for value in values)
    rank = (len(ordered) - 1) * fraction
    low = int(rank)
    high = min(low + 1, len(ordered) - 1)
    part = rank - low
    return ordered[low] + (ordered[high] - ordered[low]) * part


def describe(values: Sequence[float]) -> dict[str, float]:
    if not values:
        raise ValueError("statistics require at least one value")
    return {
        "median": percentile(values, 0.5),
        "min": min(values),
        "q1": percentile(values, 0.25),
        "q3": percentile(values, 0.75),
        "iqr": percentile(values, 0.75) - percentile(values, 0.25),
    }


def interleaved_schedule(rounds: int) -> list[str]:
    if rounds < 1:
        raise ValueError("rounds must be positive")
    return ["BA" if index % 2 else "AB" for index in range(rounds)]


def bootstrap_median_interval(
    ratios: Sequence[float], *, iterations: int = 5000, seed: int = 743
) -> tuple[float, float]:
    if not ratios:
        raise ValueError("at least one paired ratio is required")
    if len(ratios) == 1:
        return float(ratios[0]), float(ratios[0])
    if iterations < 100:
        raise ValueError("iterations must be at least 100")
    rng = random.Random(seed)
    samples = [
        percentile([rng.choice(ratios) for _ in ratios], 0.5) for _ in range(iterations)
    ]
    return percentile(samples, 0.025), percentile(samples, 0.975)


def spreads_overlap(before: dict[str, float], after: dict[str, float]) -> bool:
    return before["q1"] <= after["q3"] and after["q1"] <= before["q3"]


def parse_top_cpu(text: str) -> list[float]:
    """Return busy percentages from macOS top CPU summary lines."""
    idles = re.findall(r"CPU usage:.*?([0-9]+(?:\.[0-9]+)?)% idle", text)
    if not idles:
        raise ValueError("no macOS top CPU usage lines found")
    return [max(0.0, min(100.0, 100.0 - float(idle))) for idle in idles]


def parse_proc_stat(text: str) -> tuple[int, int]:
    """Return aggregate Linux /proc/stat total and idle+iowait counters."""
    for line in text.splitlines():
        fields = line.split()
        if fields and fields[0] == "cpu":
            counters = [int(value) for value in fields[1:9]]
            if len(counters) < 4:
                raise ValueError("incomplete aggregate cpu counters")
            total = sum(counters)
            idle = counters[3] + (counters[4] if len(counters) > 4 else 0)
            return total, idle
    raise ValueError("aggregate cpu line not found")


def proc_stat_busy(before: tuple[int, int], after: tuple[int, int]) -> float:
    total = after[0] - before[0]
    idle = after[1] - before[1]
    if total <= 0 or idle < 0:
        raise ValueError("invalid /proc/stat sample interval")
    return max(0.0, min(100.0, 100.0 * (total - idle) / total))


def sample_load(seconds: int = 3) -> LoadSample:
    started = time.monotonic()
    system = platform.system()
    if system == "Darwin":
        result = subprocess.run(
            ["top", "-l", str(seconds + 1), "-n", "0", "-stats", "cpu"],
            check=True,
            capture_output=True,
            text=True,
            timeout=seconds + 8,
        )
        samples = parse_top_cpu(result.stdout)
        return LoadSample(
            sum(samples[-seconds:]) / min(seconds, len(samples)),
            "top",
            time.monotonic() - started,
        )
    if system == "Linux":
        with open("/proc/stat", encoding="ascii") as stream:
            before = parse_proc_stat(stream.read())
        time.sleep(seconds)
        with open("/proc/stat", encoding="ascii") as stream:
            after = parse_proc_stat(stream.read())
        return LoadSample(
            proc_stat_busy(before, after), "/proc/stat", time.monotonic() - started
        )
    raise RuntimeError(f"CPU load sampling is not implemented for {system}")


def machine_metadata(compiler: str, ffmpeg_version: str) -> dict[str, str | int]:
    system = platform.system()
    cpu = platform.processor() or "unknown"
    if system == "Darwin":
        cpu = _command_text(["sysctl", "-n", "machdep.cpu.brand_string"]) or cpu
        cores_text = _command_text(["sysctl", "-n", "hw.logicalcpu"])
        cores = int(cores_text) if cores_text.isdigit() else (os_cpu_count())
    elif system == "Linux":
        try:
            with open("/proc/cpuinfo", encoding="utf-8") as stream:
                for line in stream:
                    if line.lower().startswith("model name"):
                        cpu = line.split(":", 1)[1].strip()
                        break
        except OSError:
            pass
        cores = os_cpu_count()
    else:
        cores = os_cpu_count()
    if system == "Darwin":
        macos_version = platform.mac_ver()[0]
        os_version = (
            f"macOS {macos_version} (Darwin {platform.release()})"
            if macos_version
            else f"Darwin {platform.release()}"
        )
    else:
        os_version = f"{system} {platform.release()} ({platform.version()})"
    return {
        "cpu": cpu,
        "cores": cores,
        "os": os_version,
        "compiler": compiler,
        "ffmpeg": ffmpeg_version,
    }


def _command_text(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command, check=True, capture_output=True, text=True, timeout=3
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout.strip()


def os_cpu_count() -> int:
    import os

    return os.cpu_count() or 1


def summarize_rounds(rows: Iterable[dict], *, seed: int = 743) -> list[dict]:
    groups: dict[str, list[dict]] = {}
    for row in rows:
        groups.setdefault(row["case"], []).append(row)
    results = []
    for index, (case, samples) in enumerate(sorted(groups.items())):
        before = [sample["before_ns"] / sample["frames"] for sample in samples]
        after = [sample["after_ns"] / sample["frames"] for sample in samples]
        before_stats, after_stats = describe(before), describe(after)
        ratios = [b / a for b, a in zip(before, after) if a > 0]
        low, high = bootstrap_median_interval(ratios, seed=seed + index)
        results.append(
            {
                "case": case,
                "rounds": len(samples),
                "before": before_stats,
                "after": after_stats,
                "speedup": percentile(ratios, 0.5),
                "speedup_95ci": [low, high],
                "noise_inconclusive": spreads_overlap(before_stats, after_stats),
            }
        )
    return results


def markdown_report(
    *,
    metadata: dict,
    load_before: str,
    load_during: str,
    base_sha: str,
    head_sha: str,
    cases: list[dict],
    not_a_measurement: bool = False,
) -> str:
    lines = []
    if not_a_measurement:
        lines.extend(["**NOT A MEASUREMENT — dry timing path smoke test only.**", ""])
    lines.extend(
        [
            "### Scaler before/after benchmark",
            "",
            f"- CPU: {metadata['cpu']} ({metadata['cores']} logical cores)",
            f"- OS: {metadata['os']}",
            f"- Compiler: {metadata['compiler']}",
            f"- FFmpeg: {metadata['ffmpeg']}",
            f"- Whole-machine CPU load: before {load_before}; during {load_during}",
            f"- Base SHA: `{base_sha}`",
            f"- PR head SHA: `{head_sha}`",
            "",
        ]
    )
    if not_a_measurement:
        lines.extend(
            [
                "| Case | Correctness | One-round smoke timings (ns/frame; not performance data) |",
                "| --- | --- | --- |",
            ]
        )
        for case in cases:
            lines.append(
                f"| {case['case']} | byte-identical | {case['smoke_before_ns']} / {case['smoke_after_ns']} |"
            )
    else:
        lines.extend(
            [
                "| Case | Before median (min, IQR) ns/frame | After median (min, IQR) ns/frame | Speed-up (95% bootstrap interval) |",
                "| --- | ---: | ---: | ---: |",
            ]
        )
        for case in cases:
            before, after = case["before"], case["after"]
            interval = case["speedup_95ci"]
            note = " — noise: inconclusive" if case["noise_inconclusive"] else ""
            lines.append(
                f"| {case['case']} | {before['median']:.1f} ({before['min']:.1f}, {before['iqr']:.1f}) | "
                f"{after['median']:.1f} ({after['min']:.1f}, {after['iqr']:.1f}) | "
                f"{case['speedup']:.3f}x [{interval[0]:.3f}, {interval[1]:.3f}]{note} |"
            )
    lines.extend(["", "Please paste this block as a comment on the PR."])
    return "\n".join(lines) + "\n"
