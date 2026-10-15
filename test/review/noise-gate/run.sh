#!/usr/bin/env bash
# CPU-only exactness checks. No commits, pushes, uploads, or timing by default.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
mode="${1:---check}"
case "$mode" in --check|--measure) ;; *) echo 'usage: run.sh [--check|--measure]' >&2; exit 2 ;; esac
cc="${CC:-clang}"
command -v "$cc" >/dev/null
logs="$(mktemp -d "${TMPDIR:-/tmp}/noise-gate-check.XXXXXX")"
echo "Logs: $logs"
GIT_CONFIG_GLOBAL=/dev/null git -c core.excludesFile= rev-parse HEAD > "$logs/head.txt"
git hash-object plugins/obs-filters/noise-gate-process.h > "$logs/source-blob.txt"
"$cc" --version > "$logs/compiler.txt"
flags=(-std=c11 -fno-fast-math -ffp-contract=off -Wall -Wextra -Werror -DEXPECT_VECTOR_PATH)
if [[ "$(uname -s)" == Darwin && "$(uname -m)" == x86_64 ]]; then
  flags+=(-arch x86_64)
fi
"$cc" "${flags[@]}" -O2 -DNDEBUG test/cmocka/test_noise_gate_process.c -lm -o "$logs/optimized"
"$logs/optimized" | tee "$logs/optimized.log"
"$cc" "${flags[@]}" -O1 -g -fsanitize=address,undefined test/cmocka/test_noise_gate_process.c -lm -o "$logs/sanitized"
"$logs/sanitized" | tee "$logs/sanitized.log"
if [[ "$mode" == --measure ]]; then
  [[ "$(uname -s)" == Linux ]] || { echo 'Timing adapter requires Linux; no timing run performed.' >&2; exit 77; }
  "$cc" "${flags[@]}" -O2 -DNDEBUG test/review/noise-gate/bench.c -lm -o "$logs/benchmark"
  uv run --no-project python test/review/noise-gate/measure.py --measure --binary "$logs/benchmark" \
    --compiler "$cc" --output "$logs/timings" | tee "$logs/timings.log"
fi
