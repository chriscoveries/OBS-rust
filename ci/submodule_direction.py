#!/usr/bin/env -S uv run --script
"""Fail when a change moves a submodule pointer backwards (obs-rust/obs-studio#693).

    uv run ci/submodule_direction.py <base-ref> [<head-ref>]

Every gitlink that differs between <base-ref> and <head-ref> (default HEAD)
must point at a descendant of the base's commit. Stale checkouts moved
plugins/obs-websocket back and silently undid merged fixes (#492, #496, #503,
#615). A deliberate move back is allowed when $PR_TITLE contains `submodule:`.
Commits missing from the submodule checkout are fetched from its remote;
a submodule that is not checked out is cloned bare into a temporary directory.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVERRIDE = "submodule:"


def git(cwd: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=check)


def gitlinks(repo: Path, ref: str) -> dict[str, str]:
    out = git(repo, "ls-tree", "-r", ref).stdout
    links = {}
    for line in out.splitlines():
        meta, path = line.split("\t", 1)
        mode, kind, sha = meta.split()
        if mode == "160000" and kind == "commit":
            links[path] = sha
    return links


def submodule_url(repo: Path, ref: str, path: str) -> str | None:
    blob = git(repo, "show", f"{ref}:.gitmodules", check=False)
    if blob.returncode != 0:
        return None
    with tempfile.NamedTemporaryFile("w", suffix=".gitmodules", delete=False) as f:
        f.write(blob.stdout)
    try:
        cfg = git(repo, "config", "-f", f.name, "--get-regexp", r"^submodule\..*\.(path|url)$", check=False).stdout
    finally:
        os.unlink(f.name)
    names = {}
    urls = {}
    for line in cfg.splitlines():
        key, value = line.split(" ", 1)
        name, field = key[len("submodule.") :].rsplit(".", 1)
        (names if field == "path" else urls)[name] = value
    for name, p in names.items():
        if p == path:
            return urls.get(name)
    return None


def has_commits(repo: Path, *shas: str) -> bool:
    return all(git(repo, "cat-file", "-e", f"{s}^{{commit}}", check=False).returncode == 0 for s in shas)


def is_ancestor(sub: Path, old: str, new: str) -> bool:
    return git(sub, "merge-base", "--is-ancestor", old, new, check=False).returncode == 0


def backwards(repo: Path, base: str, head: str = "HEAD", title: str = "") -> list[str]:
    """Problems, one per gitlink that moved to a non-descendant of its base pointer."""
    if OVERRIDE in title:
        return []
    before = gitlinks(repo, base)
    after = gitlinks(repo, head)
    problems = []
    for path, new in sorted(after.items()):
        old = before.get(path)
        if old is None or old == new:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            sub = repo / path
            if not (sub / ".git").exists():
                url = submodule_url(repo, head, path)
                if url is None:
                    problems.append(f"{path}: cannot find its URL in .gitmodules to check {old[:9]} -> {new[:9]}")
                    continue
                sub = Path(tmp) / "clone"
                git(Path(tmp), "clone", "-q", "--bare", url, str(sub), check=False)
            if not has_commits(sub, old, new):
                git(sub, "fetch", "-q", "origin", check=False)
                for sha in (old, new):
                    if not has_commits(sub, sha):
                        git(sub, "fetch", "-q", "origin", sha, check=False)
            if not has_commits(sub, old, new):
                problems.append(f"{path}: cannot fetch {old[:9]} and {new[:9]} to check the move")
            elif not is_ancestor(sub, old, new):
                problems.append(
                    f"{path}: moves backwards from {old[:9]} to {new[:9]}, which does not descend from the "
                    f"base pointer; this would undo merged submodule changes. Update the submodule to a "
                    f"descendant (git submodule update), or put `{OVERRIDE}` in the PR title if the move "
                    f"is deliberate."
                )
    return problems


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    head = argv[2] if len(argv) == 3 else "HEAD"
    problems = backwards(ROOT, argv[1], head, os.environ.get("PR_TITLE", ""))
    for problem in problems:
        print(f"::error::{problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
