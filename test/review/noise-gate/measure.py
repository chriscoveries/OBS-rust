#!/usr/bin/env -S uv run --script
# SPDX-License-Identifier: GPL-2.0-or-later
"""Noise-gate adapter for the shared tools/ab_bench statistics. Explicit timing only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from ab_bench import core


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--measure", action="store_true", required=True)
    args = parser.parse_args()
    if platform.system() != "Linux":
        raise SystemExit("This timing adapter currently requires Linux; CPU parity tests are portable.")
    compiler = subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0]
    metadata = core.machine_metadata(compiler, "not used")
    metadata["os"] = platform.system() + " " + platform.release()
    metadata["affinity_logical_cpus"] = len(os.sched_getaffinity(0))
    metadata["flags"] = "-std=c11 -O2 -DNDEBUG -fno-fast-math -ffp-contract=off"
    data = (ROOT / "plugins/obs-filters/noise-gate-process.h").read_bytes()
    metadata["source_blob"] = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    args.output.mkdir(parents=True, exist_ok=False)
    before = core.sample_load(1)
    rows = []
    for round_no, order in enumerate(core.interleaved_schedule(9)):
        for channels in (2, 6):
            start = core.parse_proc_stat(Path("/proc/stat").read_text())
            row = json.loads(subprocess.check_output([str(args.binary.resolve()), str(channels), order], text=True))
            end = core.parse_proc_stat(Path("/proc/stat").read_text())
            if row["output_equal"] is not True or row["frames"] != 8192:
                raise SystemExit("Timing result failed parity or work-count validation")
            row["round"] = round_no
            row["load_during_percent"] = core.proc_stat_busy(start, end)
            rows.append(row)
            with (args.output / "rounds.jsonl").open("a") as stream:
                stream.write(json.dumps(row, sort_keys=True) + "\n")
    results = core.summarize_rounds(rows)
    receipt = {
        "label": "Indicative microbenchmark, not an end-to-end measurement",
        "metadata": metadata,
        "load_before_percent": before.busy_percent,
        "load_during_percent": core.describe([r["load_during_percent"] for r in rows]),
        "load_note": "Aggregate whole-machine busy percentage includes this benchmark; no process identities collected.",
        "results": results,
    }
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
