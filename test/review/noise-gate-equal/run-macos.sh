#!/usr/bin/env bash
# Uses preconfigured OFF/ON build directories. Creates no commits and pushes nothing.
set -euo pipefail
if [[ "$(uname -s)" != Darwin || $# != 2 ]]; then
  echo 'usage: bash test/review/noise-gate-equal/run-macos.sh OFF_BUILD ON_BUILD' >&2
  exit 2
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
git -C "$root" rev-parse --is-inside-work-tree >/dev/null
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-noise-gate-equal.XXXXXX")"
git -C "$root" rev-parse HEAD > "$logs/head.txt"
git -C "$root" status --porcelain > "$logs/worktree.txt"
printf 'Logs: %s\n' "$logs"
cat > "$logs/entitlements.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.cs.disable-library-validation</key><true/></dict></plist>
PLIST
(
  cd "$root"
  clang-format-22 --dry-run --Werror plugins/obs-filters/noise-gate-filter.c test/cmocka/test_noise_gate_equal_threshold.c
  gersemi --check test/cmocka/tests.d/test_noise_gate_equal_threshold.cmake
  soldr cargo fmt --check
  soldr cargo clippy --all-targets -- -D warnings
  git diff --check
) 2>&1 | tee "$logs/format.log"
mode=OFF
for supplied in "$@"; do
  build="$(cd "$supplied" && pwd)"
  grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$root" "$build/CMakeCache.txt" || {
    echo 'Build directory belongs to a different source worktree' >&2
    exit 2
  }
  grep -Eq "^ENABLE_RUST_LIBOBS:BOOL=$mode$" "$build/CMakeCache.txt" || {
    echo "Build cache must have ENABLE_RUST_LIBOBS=$mode" >&2
    exit 2
  }
  cmake -S "$root" -B "$build" 2>&1 | tee "$logs/configure-$mode.log"
  cmake --build "$build" --config RelWithDebInfo --parallel 2 \
    --target libobs test_noise_gate_equal_threshold 2>&1 | tee "$logs/build-$mode.log"
  ctest --test-dir "$build" -C RelWithDebInfo --show-only=json-v1 -R '^test_noise_gate_equal_threshold$' > "$logs/selection-$mode.json"
  exe="$(uv run --offline --no-project python - "$logs/selection-$mode.json" <<'PYTHON'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as source:
    tests = json.load(source)["tests"]
if len(tests) != 1 or tests[0]["name"] != "test_noise_gate_equal_threshold":
    raise SystemExit("Expected exactly one native regression test")
print(tests[0]["command"][0])
PYTHON
)"
  [[ "$exe" == "$build"/* ]] || { echo 'Refusing to sign a test outside the configured build' >&2; exit 2; }
  libobs="$(find "$build" -path '*libobs.framework/*' -name libobs -type f -print -quit)"
  avcodec="$(find "$root/.deps" -name 'libavcodec*.dylib' -print -quit)"
  [[ -n "$exe" && -n "$libobs" && -n "$avcodec" ]] || {
    echo 'Test executable or required library directory is missing' >&2
    exit 1
  }
  for rpath in "$(dirname "$exe")" "${libobs%%/libobs.framework/*}" "$(dirname "$avcodec")"; do
    existing_rpaths="$(otool -l "$exe" | awk '/cmd LC_RPATH/{getline; getline; sub(/^[[:space:]]*path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print}')"
    if ! grep -Fxq "$rpath" <<< "$existing_rpaths"; then
      install_name_tool -add_rpath "$rpath" "$exe"
    fi
  done
  codesign --force --sign - --entitlements "$logs/entitlements.plist" "$exe"
  ctest --test-dir "$build" -C RelWithDebInfo --output-on-failure \
    --no-tests=error --repeat until-fail:10 -R '^test_noise_gate_equal_threshold$' 2>&1 | tee "$logs/test-$mode.log"
  mode=ON
done
printf 'Logs: %s\n' "$logs"
