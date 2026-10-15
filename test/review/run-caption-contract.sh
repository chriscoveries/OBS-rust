#!/usr/bin/env bash
# Run from a worktree of the fork-review branch. No pushing or network service use.
set -euo pipefail
cd "$(dirname "$0")/../.."
command -v soldr >/dev/null || { echo 'soldr is required' >&2; exit 2; }
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-caption-contract.XXXXXX")"
printf 'Logs: %s\n' "$logs"
git rev-parse HEAD | tee "$logs/head.txt"
for mode in debug release; do
  args=()
  [[ "$mode" == release ]] && args+=(--release)
  soldr cargo test -p obs-captions --locked "${args[@]}" 2>&1 | tee "$logs/$mode.log"
done
soldr cargo clippy -p obs-captions --all-targets --locked -- -D warnings 2>&1 | tee "$logs/clippy.log"
soldr cargo fmt -p obs-captions --check 2>&1 | tee "$logs/fmt.log"
git diff --check
