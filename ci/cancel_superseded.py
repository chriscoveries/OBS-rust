#!/usr/bin/env -S uv run --script
"""Cancel merge-queue runs that a newer queue run for the same PR replaced (#986).

    uv run ci/cancel_superseded.py [--dry-run]

Run as the first job of a merge_group run (GITHUB_RUN_ID, GITHUB_REF,
GITHUB_REPOSITORY, GITHUB_WORKFLOW_REF set; gh authenticated with a token
that has actions: write). A queue ref carries the base commit,
gh-readonly-queue/master/pr-<N>-<sha>, so the workflow's concurrency group
never pairs a PR's rebuilt queue run with its old one. When master moves,
GitHub starts a new run per queued PR and the old ones would keep waiting
for runners. The newest queue run for a PR is the live one, so every older
in-flight queue run for the same PR is cancelled. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

QUEUE_RE = re.compile(r"(?:^|/)gh-readonly-queue/[^/]+/pr-(\d+)-[0-9a-f]+$")
LIVE = {"queued", "in_progress", "waiting", "pending", "requested"}


def queue_pr(ref: str) -> int | None:
    """The PR number of a merge-queue ref, else None."""
    m = QUEUE_RE.search(ref)
    return int(m.group(1)) if m else None


def superseded(runs: list[dict], current: dict) -> list[int]:
    """Ids of in-flight queue runs for current's PR that started before it."""
    pr = queue_pr(current["head_branch"])
    if pr is None:
        return []
    return [
        r["id"]
        for r in runs
        if r["id"] != current["id"]
        and r.get("status") in LIVE
        and queue_pr(r.get("head_branch") or "") == pr
        and r["created_at"] < current["created_at"]
    ]


def gh(*args: str) -> str:
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    current = json.loads(gh("api", f"repos/{repo}/actions/runs/{run_id}"))
    workflow_id = current["workflow_id"]
    runs: list[dict] = []
    for status in ("queued", "in_progress", "waiting", "pending", "requested"):
        out = gh("api", f"repos/{repo}/actions/workflows/{workflow_id}/runs?event=merge_group&status={status}&per_page=100")
        runs += json.loads(out)["workflow_runs"]
    stale = superseded(runs, current)
    print(f"PR #{queue_pr(current['head_branch'])}: cancelling {len(stale)} superseded queue run(s): {stale}")
    for rid in stale:
        if not args.dry_run:
            # A run that finished meanwhile can't be cancelled; that's fine.
            subprocess.run(["gh", "api", "-X", "POST", f"repos/{repo}/actions/runs/{rid}/cancel"], check=False, stdout=subprocess.DEVNULL)
    return 0


if __name__ == "__main__":
    sys.exit(main())
