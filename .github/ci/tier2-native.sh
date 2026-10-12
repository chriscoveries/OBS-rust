#!/bin/bash
# Fork-only Tier 2, run directly on the runner host (no Docker). Mirrors
# rust/tools/linux-validate/validate.sh: builds libobs, the cmocka tests and
# decklink with ENABLE_RUST_LIBOBS=OFF and =ON, runs ctest in both, checks the
# Rust object swap and the Debug staticlib profile, and fails unless both builds
# export the same dynamic symbols from libobs. The host needs the packages from
# rust/tools/linux-validate/Dockerfile plus a Rust toolchain.
# Usage: tier2-native.sh SRC [--repeat N] [--tests REGEX]
# TIER2_EXTRA_CMAKE adds host-specific cmake flags.
# Build dirs live under TIER2_ROOT (default ~/.cache/obsrust-tier2/<runner>),
# so several runners on one host never share a build tree.
set -euo pipefail

# Tests that call obs_startup() need an X display on Linux. Upstream's Tier 2
# image runs them under xvfb-run; do the same when it is installed.
xvfb=""
if command -v xvfb-run >/dev/null 2>&1; then
  xvfb="xvfb-run -a"
fi

src=$(cd "$1" && pwd)
shift
repeat=0
tests=
while [ $# -gt 0 ]; do
  case "$1" in
  --repeat) repeat="${2:-}"; shift 2 || { echo "--repeat needs a count" >&2; exit 2; } ;;
  --tests) tests="${2:-}"; shift 2 || { echo "--tests needs a regex" >&2; exit 2; } ;;
  *) echo "usage: tier2-native.sh SRC [--repeat N] [--tests REGEX]" >&2; exit 2 ;;
  esac
done
[[ "$repeat" =~ ^[0-9]+$ ]] || { echo "--repeat must be a non-negative integer" >&2; exit 2; }
if [ -n "$tests" ] && [ "$repeat" -eq 0 ]; then echo "--tests only applies with --repeat" >&2; exit 2; fi

root="${TIER2_ROOT:-$HOME/.cache/obsrust-tier2/${RUNNER_NAME:-local}}"
mkdir -p "$root"
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/obsrust-ccache}"
export CCACHE_BASEDIR="$src" CCACHE_COMPRESS=1 CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-5G}"
export CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime
ccache -z >/dev/null

flags=(-DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF
  -DENABLE_WEBSOCKET=OFF -DENABLE_AJA=OFF -DENABLE_WEBRTC=OFF
  -DENABLE_NVENC=OFF -DENABLE_QSV11=OFF -DENABLE_VST=OFF -DENABLE_WAYLAND=OFF)
# Host-specific extras, e.g. rd4 (Ubuntu 22.04) lacks GIO 2.76 and librist.
read -r -a extra <<<"${TIER2_EXTRA_CMAKE:-}"
flags+=(${extra[@]+"${extra[@]}"})

for mode in OFF ON; do
  build="$root/rust-$mode"
  echo "== [$mode] configure + build"
  cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_UNIT_TESTS=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DENABLE_RUST_LIBOBS="$mode" "${flags[@]}" \
    >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
  cmake --build "$build" --target libobs cmocka-tests decklink \
    >"$build.build.log" 2>&1 || { grep -E "error|Error" "$build.build.log" | head -40; exit 1; }

  # Most tests run ${CTEST_JOBS:-4}-wide. Tests that measure timing or drive
  # real playback/capture/encoding run in a second, narrower pass after the
  # CPU-heavy tests finish. They mostly wait on real time (about 270 s of
  # sleeps in total, the longest about 75 s), so 3-wide keeps their timing intact.
  serial='clock|drift|sync|deinterlace|vlc|ffmpeg|media|transition|portal|pipewire|v4l2|restart'
  echo "== [$mode] ctest -j${CTEST_JOBS:-4}, then timing-sensitive tests -j${CTEST_TIMED_JOBS:-3}${xvfb:+ (under xvfb-run)}"
  rc=0
  $xvfb ctest --test-dir "$build" --output-on-failure --timeout 600 -j "${CTEST_JOBS:-4}" -E "$serial" || rc=$?
  $xvfb ctest --test-dir "$build" --output-on-failure --timeout 600 -j "${CTEST_TIMED_JOBS:-3}" -R "$serial" || rc=$?
  [ "$rc" -eq 0 ] || exit "$rc"
  if [ "$repeat" -gt 0 ]; then
    echo "== [$mode] ctest --repeat until-fail:$repeat${tests:+ -R $tests}"
    $xvfb ctest --test-dir "$build" --output-on-failure --timeout 600 --repeat "until-fail:$repeat" ${tests:+-R "$tests"}
  fi

  lib=$(ls "$build"/libobs/libobs.so.* | head -1)
  nm -D --defined-only "$lib" | awk '{print $3}' | sort >"$root/exports-$mode.txt"

  # The build tree persists across runs, so ask Ninja for the current graph
  # rather than trusting whatever .o files are on disk.
  # `ninja -t inputs` needs Ninja >= 1.11. Fail loudly rather than let an empty
  # list pass the ON check vacuously.
  inputs=$(ninja -C "$build" -t inputs libobs) || { echo "FAIL: ninja -t inputs failed (needs Ninja >= 1.11)"; exit 1; }
  libobs_objs=$(grep -E '\.c\.o$' <<<"$inputs" || true)
  [ -n "$libobs_objs" ] || { echo "FAIL: no libobs C objects in the Ninja graph"; exit 1; }
  if [ "$mode" = ON ]; then
    for obj in bitstream.c.o array-serializer.c.o path-extension.c.o base.c.o file-serializer.c.o pipe.c.o pipe-posix.c.o buffered-file-serializer.c.o; do
      if grep -q "/$obj\$" <<<"$libobs_objs"; then
        echo "FAIL: $obj was compiled into libobs with ENABLE_RUST_LIBOBS=ON"
        exit 1
      fi
    done
  fi
  if [ "$mode" = OFF ] && ! grep -q '/path-extension.c.o$' <<<"$libobs_objs"; then
    echo "FAIL: path-extension.c.o was not compiled into libobs with ENABLE_RUST_LIBOBS=OFF"
    exit 1
  fi
  plugin=$(find "$build" -name 'decklink.so' | head -1)
  echo "== [$mode] decklink.so imports:"
  nm -D --undefined-only "$plugin" | grep bitstream_reader
done

# Issue #80: Debug libobs must still link the release-profile (panic=abort)
# Rust staticlib. Configure only, then inspect the link line.
echo "== [ON/Debug] Rust staticlib profile"
dbg="$root/rust-ON-debug"
cmake -S "$src" -B "$dbg" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_UNIT_TESTS=OFF -DENABLE_RUST_LIBOBS=ON "${flags[@]}" \
  >"$dbg.configure.log" 2>&1 || { tail -40 "$dbg.configure.log"; exit 1; }
rust_archives=$(grep -ohE '/cargo/build/[^ $]*/liblibobs_rust\.a' "$dbg/build.ninja" | sort -u)
echo "$rust_archives"
if [ -z "$rust_archives" ] || grep -qv '/release/liblibobs_rust\.a$' <<<"$rust_archives"; then
  echo "FAIL: Debug libobs links a non-release (panic=unwind) Rust staticlib"
  exit 1
fi

echo "== libobs exported symbols, OFF vs ON"
diff "$root/exports-OFF.txt" "$root/exports-ON.txt"
echo "IDENTICAL ($(wc -l <"$root/exports-ON.txt") symbols)"

echo "== ccache"
ccache -s | grep -E "Hits|Misses|Cache size" || true
