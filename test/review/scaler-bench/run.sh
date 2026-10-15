#!/usr/bin/env bash
# --quick performs correctness/path checks; --measure runs 1080p/4K timed cases.
set -euo pipefail
cd "$(dirname "$0")/../../.."
mode="${1:---quick}"
[[ "$mode" == --quick || "$mode" == --measure ]] || { echo 'Use --quick or --measure' >&2; exit 2; }
command -v uv >/dev/null
command -v cmake >/dev/null
command -v pkg-config >/dev/null || { echo 'pkg-config is required' >&2; exit 2; }
# Reuse the OBS dependency bundle if the caller has not selected pkg-config paths.
if ! pkg-config --exists libswscale libavutil; then
  while IFS= read -r pc; do
    export PKG_CONFIG_PATH="$(dirname "$pc")${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    pkg-config --exists libswscale libavutil && break
  done < <(find "$PWD/.deps" -name libswscale.pc -type f 2>/dev/null || true)
fi
pkg-config --exists libswscale libavutil || { echo 'FFmpeg development headers/pkg-config files required' >&2; exit 2; }
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-scaler-bench.XXXXXX")"
echo "Logs: $logs"
git rev-parse HEAD > "$logs/head.txt"
git status --porcelain > "$logs/worktree.txt"
git hash-object libobs/media-io/video-scaler-ffmpeg.c > "$logs/scaler-blob.txt"
uname -a > "$logs/host.txt"
ps -Ao pid,pcpu,comm > "$logs/processes-before.txt"
uv run test/review/scaler-bench/test_summary.py 2>&1 | tee "$logs/unit-tests.log"
cmake -S test/review/scaler-bench -B "$logs/build" -DCMAKE_BUILD_TYPE=Release 2>&1 | tee "$logs/configure.log"
cmake --build "$logs/build" --parallel 2 2>&1 | tee "$logs/build.log"
if [[ "$mode" == --measure ]]; then
  "$logs/build/obs-scaler-bench" --frames "${SCALER_BENCH_FRAMES:-30}" > "$logs/results.jsonl"
else
  "$logs/build/obs-scaler-bench" --quick > "$logs/results.jsonl"
fi
ps -Ao pid,pcpu,comm > "$logs/processes-after.txt"
uv run test/review/scaler-bench/summarize.py "$logs/results.jsonl" | tee "$logs/report.json"
