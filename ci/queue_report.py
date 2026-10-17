#!/usr/bin/env -S uv run --script
"""Report a merge-queue run's wall time and rebuilds in its job summary (#930 E1).

    uv run ci/queue_report.py

Run in CI Gate on merge_group runs (GITHUB_RUN_ID, GITHUB_REPOSITORY and,
for the summary, GITHUB_STEP_SUMMARY set; gh authenticated with a token
that has actions: read). Writes the run's wall time (from its creation),
the runner time its finished jobs took, the slowest jobs, and which queue
attempt this is for the PR: attempt 2 or later means master moved under the
PR (a merge ahead of it failed, or a bypass merged outside the queue) and
it was rebuilt. Informational only: the step never fails CI Gate.
Standard library only.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cancel_superseded import queue_pr  # noqa: E402


def _t(stamp: str) -> datetime:
    return datetime.fromisoformat(stamp.replace("Z", "+00:00"))


def attempt(runs: list[dict], current: dict) -> tuple[int, list[int]]:
    """(attempt number, ids of earlier queue runs) for current's PR."""
    pr = queue_pr(current["head_branch"])
    earlier = sorted(
        (r for r in runs
         if r["id"] != current["id"]
         and queue_pr(r.get("head_branch") or "") == pr
         and r["created_at"] < current["created_at"]),
        key=lambda r: r["created_at"],
    )
    return len(earlier) + 1, [r["id"] for r in earlier]


def _minutes(job: dict) -> float | None:
    if not job.get("started_at") or not job.get("completed_at"):
        return None
    return (_t(job["completed_at"]) - _t(job["started_at"])).total_seconds() / 60


def summary(current: dict, jobs: list[dict], att: tuple[int, list[int]], now: str) -> str:
    wall = (_t(now) - _t(current["created_at"])).total_seconds() / 60
    timed = [(m, j["name"]) for j in jobs if (m := _minutes(j)) is not None]
    n, earlier = att
    lines = [
        f"### Merge queue: PR #{queue_pr(current['head_branch'])}, attempt {n}",
        "",
        f"- Wall time: {wall:.1f} min (run created to CI Gate)",
        f"- Runner time: {sum(m for m, _ in timed):.1f} min across {len(timed)} finished jobs",
    ]
    if n > 1:
        lines.append(f"- rebuilt: master moved under this PR; earlier queue runs: {', '.join(map(str, earlier))}")
    lines += ["", "| Job | min |", "|---|---|"]
    lines += [f"| {name} | {m:.1f} |" for m, name in sorted(timed, reverse=True)[:8]]
    return "\n".join(lines) + "\n"


def gh(*args: str) -> str:
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def main() -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    current = json.loads(gh("api", f"repos/{repo}/actions/runs/{run_id}"))
    runs = json.loads(gh("api", f"repos/{repo}/actions/workflows/{current['workflow_id']}/runs?event=merge_group&per_page=100"))["workflow_runs"]
    jobs = json.loads(gh("api", f"repos/{repo}/actions/runs/{run_id}/jobs?per_page=100"))["jobs"]
    now = datetime.now(timezone.utc).isoformat()
    md = summary(current, jobs, attempt(runs, current), now)
    print(md)
    if path := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(path, "a", encoding="utf-8") as f:
            f.write(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
