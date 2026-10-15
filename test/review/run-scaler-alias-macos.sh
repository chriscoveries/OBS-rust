#!/bin/bash
# Build only the scaler tests in OFF/ON configurations. No Git writes.
set -euo pipefail

script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
repo="${SCALER_REVIEW_SOURCE_DIR:-$script_root}"
cd "$repo"
[ "$(uname -s)" = Darwin ] || { echo "Run this integration check on macOS." >&2; exit 2; }
for tool in cmake brew clang git; do
  command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 2; }
done
cmocka_prefix="$(brew --prefix cmocka)"
build_root="${SCALER_REVIEW_BUILD_ROOT:-$repo/build_review_scaler}"
mkdir -p "$build_root"
build_root="$(cd "$build_root" && pwd -P)"
export XCODE_CAS_PATH="${XCODE_CAS_PATH:-$build_root/xcode-cas}"
mkdir -p "$XCODE_CAS_PATH"
printf 'Source: %s\nCommit: %s\n' "$repo" "$(git rev-parse HEAD)"
git diff --check

# This checks the exact private predicates used by the production scaler.
if [ -f test/cmocka/test_video_scaler_spans.c ]; then
  clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    test/cmocka/test_video_scaler_spans.c -o "$build_root/scaler-spans-sanitized"
  "$build_root/scaler-spans-sanitized"
fi

# An alternate source directory lets this same runner exercise the RED
# checkpoint without copying files or modifying either worktree.
for mode in ${SCALER_REVIEW_MODES:-OFF ON}; do
  case "$mode" in OFF|ON) ;; *) echo "Invalid mode: $mode" >&2; exit 2 ;; esac
  build="$build_root/$mode"
  targets=(libobs test_video_scaler test_video_scaler_alias)
  if [ -f test/cmocka/test_video_scaler_spans.c ]; then
    targets+=(test_video_scaler_spans)
  fi
  cmake --preset macos-ci -B "$build" \
    -DENABLE_UNIT_TESTS=ON -DENABLE_RUST_LIBOBS="$mode" \
    -DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF \
    -DENABLE_BROWSER=OFF -DENABLE_WEBSOCKET=OFF \
    -DCMAKE_PREFIX_PATH="$cmocka_prefix" 2>&1 | tee "$build_root/$mode-configure.log"
  cmake --build "$build" --config RelWithDebInfo --parallel "${SCALER_REVIEW_JOBS:-4}" \
    --target "${targets[@]}" 2>&1 | tee "$build_root/$mode-build.log"
  lib="$(find "$build" -path '*libobs.framework*' -name libobs -type f | head -n 1)"
  avcodec="$(find "$repo/.deps" -path '*/lib/libavcodec*.dylib' -type f | head -n 1)"
  [ -n "$lib" ] && [ -n "$avcodec" ] || { echo "Missing runtime libraries" >&2; exit 1; }
  framework_dir="${lib%%/libobs.framework/*}"
  DYLD_FRAMEWORK_PATH="$framework_dir" DYLD_LIBRARY_PATH="$(dirname "$avcodec")" \
    ctest --test-dir "$build" -C RelWithDebInfo --output-on-failure \
    -R '^test_video_scaler(_alias|_spans)?$' --repeat until-fail:10 \
    2>&1 | tee "$build_root/$mode-tests.log"
done
printf 'Scaler checks passed. Logs: %s\n' "$build_root"
