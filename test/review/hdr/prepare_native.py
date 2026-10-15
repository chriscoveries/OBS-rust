#!/usr/bin/env -S uv run --script
"""Create an ordinary, detached HDR source commit from the reviewed integration.

Only the disposable worktree is changed. Existing branch refs are never moved,
no hooks or workflow jobs run, and nothing is pushed. Git identity must already
be configured. The original integrate.py transformation is used unchanged.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

MARKER = "test/review/hdr/native-integration.json"


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=root, text=True).strip()


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def detached(root: Path) -> None:
    result = subprocess.run(["git", "symbolic-ref", "-q", "HEAD"], cwd=root, capture_output=True)
    if result.returncode != 1:
        raise RuntimeError("A detached disposable worktree is required")


def verify(root: Path, paths: set[str]) -> dict:
    manifest = json.loads((root / MARKER).read_text())
    if manifest.get("schema") != 1 or set(manifest.get("files", {})) != paths:
        raise RuntimeError("Unexpected native integration manifest")
    for name, sha in manifest["files"].items():
        if blob((root / name).read_bytes()) != sha:
            raise RuntimeError(f"Integrated source changed: {name}")
    return manifest


def commit_snapshot(root: Path, parent: str, expected: dict[str, str]) -> str:
    detached(root)
    if git(root, "rev-parse", "HEAD") != parent:
        raise RuntimeError("HEAD changed while preparing source")
    if git(root, "diff", "--cached", "--name-only"):
        raise RuntimeError("The index already contains staged changes")
    changed = set(git(root, "diff", "--name-only").splitlines())
    if changed != set(expected):
        raise RuntimeError("Tracked changes differ from the seven reviewed source edits")
    marker = root / MARKER
    if marker.exists():
        raise RuntimeError("Refusing to overwrite an integration manifest")
    for name, sha in expected.items():
        if blob((root / name).read_bytes()) != sha:
            raise RuntimeError(f"Unexpected transformed source: {name}")
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps({"schema": 1, "parent": parent, "files": expected}, indent=2, sort_keys=True) + "\n")
    git(root, "add", "--", *sorted(expected), MARKER)
    staged = set(git(root, "diff", "--cached", "--name-only").splitlines())
    if staged != set(expected) | {MARKER}:
        raise RuntimeError("Unexpected staged paths")
    subprocess.run(["git", "diff", "--cached", "--check"], cwd=root, check=True)
    # This is a local review snapshot, not a signed CI attestation.
    subprocess.run(["git", "-c", "core.hooksPath=/dev/null", "-c", "commit.gpgSign=false", "commit", "-m",
                    "hdr: integrate reviewed native sources for direct-checkout validation"], cwd=root, check=True)
    return git(root, "rev-parse", "HEAD")


def main() -> None:
    import integrate
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--patch", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    detached(root)
    if git(root, "status", "--porcelain", "--untracked-files=no"):
        raise SystemExit("REFUSED: tracked worktree changes already exist")
    if (root / MARKER).exists():
        git(root, "ls-files", "--error-unmatch", MARKER)
        verify(root, set(integrate.PINS))
        print(f"Verified committed native sources at {git(root, 'rev-parse', 'HEAD')}")
        return
    # Verify identity before altering the worktree or index.
    git(root, "var", "GIT_AUTHOR_IDENT")
    git(root, "var", "GIT_COMMITTER_IDENT")
    parent = git(root, "rev-parse", "HEAD")
    expected = {}
    for name, sha in integrate.PINS.items():
        data = (root / name).read_bytes()
        if blob(data) != sha:
            raise SystemExit(f"REFUSED: source does not match the reviewed base: {name}")
        transformed = integrate.transform(root, name, data.decode())
        expected[name] = blob(transformed.encode())
    subprocess.run(["uv", "run", "--no-project", "python", "test/review/hdr/integrate.py",
                    "--apply", "--patch", str(args.patch.resolve())], cwd=root, check=True)
    commit = commit_snapshot(root, parent, expected)
    verify(root, set(expected))
    print(f"Native source commit: {commit}")
    print("Detached HEAD only. No branch updates, upstream changes, or pushes.")


if __name__ == "__main__":
    main()
