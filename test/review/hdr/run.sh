#!/usr/bin/env bash
# Local handoff only. Never commits, changes branch heads, or pushes.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root"
mode="${1:---cpu}"
if [[ "$mode" != --cpu && "$mode" != --native ]]; then
  echo 'usage: bash test/review/hdr/run.sh [--cpu|--native]' >&2
  exit 2
fi
logs="$(mktemp -d "${TMPDIR:-/tmp}/obs-hdr-tests.XXXXXX")"
echo "HDR test logs: $logs"
cc="${CC:-clang}"
cxx="${CXX:-clang++}"
"$cc" -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
  test/cmocka/test_hdr_recording_profile.c -lm -o "$logs/profile"
"$logs/profile" | tee "$logs/profile.log"
"$cxx" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
  test/review/hdr/test_shader_math.cpp -o "$logs/shader"
"$logs/shader" | tee "$logs/shader.log"
uv run --no-project python test/review/hdr/test_verifier.py 2>&1 | tee "$logs/verifier.log"
[[ "$mode" == --native ]] || exit 0
[[ "$(uname -s)" == Darwin ]] || { echo 'Native OBS validation requires a Mac' >&2; exit 2; }
command -v soldr >/dev/null
command -v cmake >/dev/null
cmocka="$(brew --prefix cmocka)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-$(xcode-select -p)}"
[[ "$DEVELOPER_DIR" == */Contents/Developer ]] || { echo 'Select full Xcode with DEVELOPER_DIR' >&2; exit 2; }
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
[[ "$SDKROOT" == "$DEVELOPER_DIR"/Platforms/MacOSX.platform/Developer/SDKs/* ]] || { echo 'Xcode/SDK mismatch' >&2; exit 2; }
# A detached worktree keeps the reviewed branch and upstream-facing bases unchanged.
work="$logs/native"
git worktree add --detach "$work" HEAD
cd "$work"
uv run --no-project python test/review/hdr/integrate.py --apply --patch "$logs/native-integration.patch"
git submodule update --init --recursive
export XCODE_CAS_PATH="${XCODE_CAS_PATH:-$HOME/Library/Developer/Xcode/DerivedData/CompilationCache.noindex}"
mkdir -p "$XCODE_CAS_PATH"
for rust in OFF ON; do
  build="$work/build-hdr-$rust"
  cmake --preset macos-ci -B "$build" -DENABLE_RUST_LIBOBS="$rust" -DENABLE_UNIT_TESTS=ON \
    -DENABLE_HEVC=ON -DENABLE_BROWSER=OFF -DCMAKE_PREFIX_PATH="$cmocka" -DCMAKE_OSX_SYSROOT="$SDKROOT" \
    2>&1 | tee "$logs/configure-$rust.log"
  cmake --build "$build" --config RelWithDebInfo --parallel 4 \
    --target obs-studio mac-videotoolbox obs-outputs cmocka-tests \
    2>&1 | tee "$logs/build-$rust.log"
  lib="$(find "$build" -path '*libobs.framework*' -name libobs -type f -print -quit)"
  avcodec="$(find "$work/.deps" -path '*/lib/libavcodec*.dylib' -print -quit)"
  [[ -n "$lib" && -n "$avcodec" ]] || { echo 'Build libraries missing' >&2; exit 1; }
  DYLD_FRAMEWORK_PATH="${lib%%/libobs.framework/*}" DYLD_LIBRARY_PATH="$(dirname "$avcodec")" \
    ctest --test-dir "$build" -C RelWithDebInfo --output-on-failure 2>&1 | tee "$logs/ctest-$rust.log"
  nm -gU "$lib" | awk '{print $3}' | LC_ALL=C sort > "$logs/exports-$rust.txt"
done
diff -u "$logs/exports-OFF.txt" "$logs/exports-ON.txt"
echo "Native build/test gate complete. Worktree retained at $work"
echo "Next: launch built OBS, record the acceptance scene, and run verify_recording.py on the actual MP4."
echo "iPhone playback and separately approved Instagram/Facebook uploads are still required."
