#!/usr/bin/env -S uv run --script
"""Summarise complete paired scaler runs; retain raw JSONL beside the report."""
import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path


def summarize(rows):
    groups = defaultdict(list)
    modes = set()
    environments = [row for row in rows if row.get("kind") == "environment"]
    if len(environments) != 1:
        raise ValueError("Expected one environment record")
    for row in rows:
        if row.get("kind") == "environment":
            continue
        if row.get("kind") != "pair" or row.get("equal") is not True:
            raise ValueError("Unknown record or unequal output")
        for key in ("src_w", "src_h", "dst_w", "dst_h", "frames", "copy_ns", "direct_ns"):
            if type(row.get(key)) is not int or row[key] <= 0:
                raise ValueError(f"Invalid {key}")
        if type(row.get("pair")) is not int or not 0 <= row["pair"] < 8:
            raise ValueError("Invalid pair index")
        if type(row.get("quick")) is not bool:
            raise ValueError("Missing mode")
        if row.get("direct_calls") != row["frames"] or row.get("copy_calls") != row["frames"]:
            raise ValueError("The expected paths were not exercised")
        if row.get("order") != ("DC" if row["pair"] % 2 else "CD"):
            raise ValueError("Missing alternating order")
        if row.get("format") not in ("BGRA", "I420", "NV12", "P010"):
            raise ValueError("Unknown format")
        modes.add(row["quick"])
        key = tuple(row[k] for k in ("format", "src_w", "src_h", "dst_w", "dst_h"))
        groups[key].append(row)
    if len(modes) != 1 or len(groups) != 12:
        raise ValueError("Run must contain all twelve cases in one mode")
    smoke = modes.pop()
    output = []
    for key, pairs in sorted(groups.items()):
        if len(pairs) != 8 or {p["pair"] for p in pairs} != set(range(8)):
            raise ValueError("Incomplete or duplicate paired measurements")
        if len({p["frames"] for p in pairs}) != 1:
            raise ValueError("Unequal batch lengths")
        entry = dict(zip(("format", "src_w", "src_h", "dst_w", "dst_h"), key))
        entry.update(pairs=8, frames_per_batch=pairs[0]["frames"], output_equal=True)
        if not smoke:
            copy = [p["copy_ns"] / p["frames"] / 1e6 for p in pairs]
            direct = [p["direct_ns"] / p["frames"] / 1e6 for p in pairs]
            changes = [100 * (1 - p["direct_ns"] / p["copy_ns"]) for p in pairs]
            entry.update(copy_ms=statistics.median(copy), direct_ms=statistics.median(direct),
                         median_time_saved_percent=statistics.median(changes),
                         paired_percent_range=[min(changes), max(changes)])
        output.append(entry)
    return {"mode": "smoke_only" if smoke else "timings_require_host_load_review",
            "environment": environments[0], "cases": output}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jsonl", type=Path)
    args = parser.parse_args()
    try:
        report = summarize([json.loads(line) for line in args.jsonl.read_text().splitlines() if line.strip()])
    except (ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Rejected benchmark: {error}\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
