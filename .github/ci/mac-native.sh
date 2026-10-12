#!/bin/bash
# Fork-only macOS CI lane, run directly on the macdad runner host. Mirrors
# tier2-native.sh but for a single ENABLE_RUST_LIBOBS=ON config on arm64:
# builds libobs and the cmocka tests natively, runs ctest, and also runs the
# Rust suites (debug as the gate, release as report-only). Deps come from the
# persistent obs-studio/.deps directory, linked into the checkout's .deps so
# they are reused across runs. Everything runs under `nice -n 10` so the
# owner's Mac stays usable, and nothing opens a window: test/osx is never
# added by the top-level CMakeLists, and no cmocka target creates an
# NSWindow/NSApplication.
# Usage: mac-native.sh SRC
# MAC_EXTRA_CMAKE adds host-specific cmake flags.
# Build dirs live under MAC_ROOT (default ~/.cache/obsrust-mac).
set -euo pipefail

src=$(cd "$1" && pwd)

root="${MAC_ROOT:-$HOME/.cache/obsrust-mac}"
mkdir -p "$root"

# Persistent dependencies: the checkout's .deps is a symlink into the
# long-lived obs-studio checkout so obs-deps are fetched once.
deps="${MAC_DEPS_DIR:-/Users/chrisd/obsrust/obs-studio/.deps}"
if [ -d "$deps" ]; then
  ln -sfn "$deps" "$src/.deps"
fi

export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/obsrust-ccache}"
export CCACHE_BASEDIR="$src" CCACHE_COMPRESS=1 CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-5G}"
export CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime
ccache -z >/dev/null

NICE="nice -n 10"

echo "== [gate] Rust tiers 1-3, debug"
$NICE soldr cargo test --workspace --locked

echo "== [report-only] Rust tests, release profile"
set +e
$NICE soldr cargo test --workspace --locked --release --no-fail-fast
rel_rc=$?
set -e
echo "== release profile exit code: $rel_rc (report-only, not gating)"

flags=(-DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF
  -DENABLE_WEBSOCKET=OFF -DENABLE_AJA=OFF -DENABLE_WEBRTC=OFF
  -DENABLE_NVENC=OFF -DENABLE_QSV11=OFF -DENABLE_VST=OFF -DENABLE_WAYLAND=OFF)
read -r -a extra <<<"${MAC_EXTRA_CMAKE:-}"
flags+=(${extra[@]+"${extra[@]}"})

build="$root/rust-ON"
# Persistent build dir: wipe on generator mismatch (e.g. left over from a
# Ninja-configured run) so configure can't fail on a stale CMakeCache.txt.
if [ -f "$build/CMakeCache.txt" ] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Xcode$' "$build/CMakeCache.txt"; then
  rm -rf "$build"
fi
echo "== [ON] configure + build"
# OBS requires the Xcode generator on macOS (cmake/macos/compilerconfig.cmake
# hard-fails otherwise and enables Swift), so Ninja is not an option here.
# Xcode is multi-config, and OBS enables the hardened runtime only on the
# Release-family configs (cmake/macos/xcode.cmake): an adhoc-signed test
# binary then refuses the adhoc-signed homebrew cmocka dylib under library
# validation. Build the Debug config instead — no hardened runtime, no team
# ID required.
cmake -S "$src" -B "$build" -G Xcode \
  -DENABLE_UNIT_TESTS=ON -DENABLE_RUST_LIBOBS=ON \
  -DCMocka_DIR="${CMOCKA_DIR:-/opt/homebrew/lib/cmake/cmocka}" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  "${flags[@]}" \
  >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
$NICE cmake --build "$build" --config Debug --target libobs cmocka-tests \
  >"$build.build.log" 2>&1 || { grep -E "error|Error" "$build.build.log" | head -40; exit 1; }

# Test binaries and libobs itself carry LC_RPATH @executable_path/../Frameworks
# and load @rpath/libobs.framework plus @rpath/lib*.dylib from obs-deps — the
# same layout as an app bundle's Frameworks dir. Xcode puts the framework in
# libobs/Debug/ and the deps live in .deps, so link them all where the loader
# looks.
fwdir="$build/test/cmocka/Frameworks"
mkdir -p "$fwdir"
ln -sfn "$build/libobs/Debug/libobs.framework" "$fwdir/libobs.framework"
if [ -d "$src/.deps" ]; then
  find "$src/.deps" -name "*.dylib*" -type f -o -name "*.dylib*" -type l | while read -r d; do
    ln -sfn "$d" "$fwdir/$(basename "$d")"
  done
fi
echo "== [ON] ctest"
$NICE ctest --test-dir "$build" -C Debug --output-on-failure

echo "== ccache"
ccache -s | grep -E "Hits|Misses|Cache size" || true
