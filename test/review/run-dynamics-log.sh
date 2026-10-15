#!/usr/bin/env bash
# Actual production header; combines log, negative-exponent and ratio-1 regressions.
set -euo pipefail
cd "$(dirname "$0")/../.."
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo 'Requires x86-64; use an x86-64 shell under Rosetta on Apple Silicon.' >&2; exit 77 ;;
esac
cc="${CC:-clang}"
command -v "$cc" >/dev/null || { echo "Missing compiler: $cc" >&2; exit 2; }
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-dynamics-log.XXXXXX")"
echo "Logs: $logs"
git rev-parse HEAD > "$logs/head.txt"
git status --porcelain > "$logs/worktree.txt"
"$cc" --version > "$logs/compiler.txt"
git hash-object plugins/obs-filters/dynamics-simd.h > "$logs/header-blob.txt"
flags=(-std=c11 -fno-fast-math -ffp-contract=off -Wall -Wextra -Werror -Ilibobs)
[[ "$(uname -s)" != Darwin ]] || flags+=(-arch x86_64)
for test in test_dynamics_log_range test_dynamics_regression_bundle; do
  "$cc" "${flags[@]}" -O2 -DNDEBUG "test/cmocka/$test.c" -lm -o "$logs/$test-optimized"
  "$logs/$test-optimized" 2>&1 | tee "$logs/$test-optimized.log"
  "$cc" "${flags[@]}" -O1 -g -fsanitize=address,undefined "test/cmocka/$test.c" -lm -o "$logs/$test-sanitized"
  "$logs/$test-sanitized" 2>&1 | tee "$logs/$test-sanitized.log"
done
