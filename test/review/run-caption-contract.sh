#!/usr/bin/env bash
# Run from a worktree of the fork-review branch. No pushing or network service use.
set -euo pipefail
cd "$(dirname "$0")/../.."
command -v soldr >/dev/null || { echo 'soldr is required' >&2; exit 2; }
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-caption-contract.XXXXXX")"
printf 'Logs: %s\n' "$logs"
git rev-parse HEAD | tee "$logs/head.txt"
soldr cargo test -p obs-captions --locked 2>&1 | tee "$logs/debug.log"
soldr cargo test -p obs-captions --locked --release 2>&1 | tee "$logs/release.log"
soldr cargo clippy -p obs-captions --all-targets --locked -- -D warnings 2>&1 | tee "$logs/clippy.log"
soldr cargo fmt -p obs-captions --check 2>&1 | tee "$logs/fmt.log"
git diff --check
