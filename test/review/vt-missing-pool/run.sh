#!/usr/bin/env bash
# Narrow native check. Builds and signs only the local test executable.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root"
[[ "$(uname -s)" == Darwin ]] || { echo 'This test requires macOS frameworks.' >&2; exit 2; }
command -v soldr >/dev/null
command -v cmake >/dev/null
export DEVELOPER_DIR="${DEVELOPER_DIR:-$(xcode-select -p)}"
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export XCODE_CAS_PATH="${XCODE_CAS_PATH:-$root/build-review-vt-cache}"
mkdir -p "$XCODE_CAS_PATH"
cmocka="$(brew --prefix cmocka)"
logs="$(mktemp -d "${TMPDIR:-/tmp}/vt-pool-review.XXXXXX")"
printf 'Logs: %s\n' "$logs"
git rev-parse HEAD > "$logs/head.txt"
cat > "$logs/test-entitlements.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.cs.disable-library-validation</key><true/></dict></plist>
PLIST
for mode in OFF ON; do
  build="$root/build-review-vt-pool-$mode"
  cmake --preset macos-ci -B "$build" -DENABLE_RUST_LIBOBS="$mode" \
    -DENABLE_UNIT_TESTS=ON -DENABLE_HEVC=ON -DENABLE_FRONTEND=OFF \
    -DENABLE_BROWSER=OFF -DENABLE_SCRIPTING=OFF -DENABLE_WEBSOCKET=OFF \
    -DCMAKE_PREFIX_PATH="$cmocka" -DCMAKE_OSX_SYSROOT="$SDKROOT" \
    2>&1 | tee "$logs/configure-$mode.log"
  cmake --build "$build" --config RelWithDebInfo --parallel 2 \
    --target libobs test_vt_missing_pixel_pool 2>&1 | tee "$logs/build-$mode.log"
  exe="$(find "$build" -type f -name test_vt_missing_pixel_pool -print -quit)"
  lib="$(find "$build" -path '*libobs.framework*' -type f -name libobs -print -quit)"
  avcodec="$(find "$root/.deps" -path '*/lib/libavcodec*.dylib' -print -quit)"
  [[ -n "$exe" && -n "$lib" && -n "$avcodec" ]] || { echo 'Required build products missing.' >&2; exit 1; }
  for rpath in "$(dirname "$exe")" "${lib%%/libobs.framework/*}" "$(dirname "$avcodec")" "$cmocka/lib"; do
    otool -l "$exe" > "$logs/rpaths-$mode.txt"
    if ! grep -Fq "path $rpath (" "$logs/rpaths-$mode.txt"; then
      install_name_tool -add_rpath "$rpath" "$exe"
    fi
  done
  codesign --force --sign - --options runtime --entitlements "$logs/test-entitlements.plist" "$exe"
  ctest --test-dir "$build" -C RelWithDebInfo --output-on-failure \
    -R '^test_vt_missing_pixel_pool$' --repeat until-fail:10 \
    2>&1 | tee "$logs/ctest-$mode.log"
done
