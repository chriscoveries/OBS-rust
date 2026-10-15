#!/usr/bin/env bash
# Crate tests plus the actual vendored decoder; no OBS process, capture or network.
set -euo pipefail
cd "$(dirname "$0")/../.."
command -v soldr >/dev/null || { echo 'soldr is required' >&2; exit 2; }
command -v cmake >/dev/null || { echo 'CMake is required' >&2; exit 2; }
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-caption-contract.XXXXXX")"
printf 'Logs: %s\n' "$logs"
git rev-parse HEAD | tee "$logs/head.txt"
git status --porcelain > "$logs/worktree.txt"
cmake -S test/review/caption-oracle -B "$logs/oracle" -DCMAKE_BUILD_TYPE=Release 2>&1 | tee "$logs/configure.log"
cmake --build "$logs/oracle" --config Release --parallel 2 2>&1 | tee "$logs/build.log"
oracle="$logs/oracle/obs-caption-oracle"
[[ -x "$oracle" ]] || oracle="$logs/oracle/Release/obs-caption-oracle"
soldr cargo test -p obs-captions --locked 2>&1 | tee "$logs/debug.log"
soldr cargo test -p obs-captions --locked --release 2>&1 | tee "$logs/release.log"
soldr cargo run -p obs-captions --locked --example libcaption_vectors > "$logs/debug.vectors" 2> "$logs/debug.generate.log"
"$oracle" < "$logs/debug.vectors" | tee "$logs/debug.decode.log"
soldr cargo run -p obs-captions --locked --release --example libcaption_vectors > "$logs/release.vectors" 2> "$logs/release.generate.log"
"$oracle" < "$logs/release.vectors" | tee "$logs/release.decode.log"
soldr cargo clippy -p obs-captions --all-targets --locked -- -D warnings 2>&1 | tee "$logs/clippy.log"
soldr cargo fmt -p obs-captions --check 2>&1 | tee "$logs/fmt.log"
git diff --check
