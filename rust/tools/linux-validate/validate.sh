#!/bin/bash
# Tier 2 validation (docs/rust-port/testing-policy.md), run inside the
# container built from ./Dockerfile; see run.sh. Builds libobs, the cmocka
# tests and the decklink plugin with ENABLE_RUST_LIBOBS=OFF and =ON, runs the
# unchanged C tests in both, and fails unless both builds export exactly the
# same dynamic symbols from libobs. With --repeat N it then reruns the tests
# (or the --tests REGEX subset) up to N times per build to catch flakes, as
# testing-policy.md requires for threading and task.
set -euo pipefail

repeat=0
tests=
while [ $# -gt 0 ]; do
  case "$1" in
  --repeat)
    repeat="${2:-}"
    shift 2 || { echo "--repeat needs a count" >&2; exit 2; }
    ;;
  --tests)
    tests="${2:-}"
    shift 2 || { echo "--tests needs a regex" >&2; exit 2; }
    ;;
  *)
    echo "usage: validate.sh [--repeat N] [--tests REGEX]" >&2
    exit 2
    ;;
  esac
done
if ! [[ "$repeat" =~ ^[0-9]+$ ]]; then
  echo "--repeat must be a non-negative integer, got '$repeat'" >&2
  exit 2
fi
if [ -n "$tests" ] && [ "$repeat" -eq 0 ]; then
  echo "--tests only applies with --repeat" >&2
  exit 2
fi

git config --global --add safe.directory /src
mkdir -p /src
rsync -a --delete --exclude /target /ro/ /src/
# shellcheck source=stale-build.sh
source /src/rust/tools/linux-validate/stale-build.sh
# rustc as rust-toolchain.toml selects it, which is what Corrosion configures.
rustc_version=$(cd /src && rustc -V)
cd /src
for sub in plugins/obs-browser plugins/obs-websocket; do
  if [ -z "$(ls -A "$sub" 2>/dev/null)" ]; then
    git submodule update --init --depth 1 "$sub"
  fi
done

# Compiler cache: keeps cold builds (a fresh clone or another worktree, whose
# mtimes differ, or a CI runner) from recompiling OBS. CCACHE_DIR defaults to
# the build volume; run.sh points it at a host dir when OBS_CCACHE_DIR is set
# (CI restores and saves that dir with actions/cache).
export CCACHE_DIR="${CCACHE_DIR:-/build/ccache}"
export CCACHE_BASEDIR=/src CCACHE_COMPRESS=1 CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-2G}"
export CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime
ccache -z >/dev/null

# Every libobs C file the Rust build replaces, derived from the
# ENABLE_RUST_LIBOBS guards (a NOT around the option means "left out when
# ON") instead of a hand-kept list (obs-rust/obs-studio#137). Included-when-ON
# files such as util/base-variadic.c have no NOT and are skipped. Paths stay
# relative to libobs/ and are matched under libobs.dir/, because libobs also
# links same-named files from deps (deps/libcaption/src/utf8.c).
ported_objs=$(grep -hE 'ENABLE_RUST_LIBOBS' /src/libobs/CMakeLists.txt /src/libobs/cmake/os-linux.cmake |
  grep 'NOT:' | grep -oE ':[A-Za-z0-9_./-]+\.c>' | sed -E 's|^:||; s|>$||; s|$|.o|' | sort -u)
if [ "$(wc -w <<<"$ported_objs")" -lt 20 ]; then
  echo "FAIL: derived only $(wc -w <<<"$ported_objs") ported objects from the ENABLE_RUST_LIBOBS guards"
  exit 1
fi
echo "== ported objects checked with ENABLE_RUST_LIBOBS=ON: $(wc -w <<<"$ported_objs")"

# OBS_TIER2_MODES: which ENABLE_RUST_LIBOBS builds to run (default both). CI
# runs OFF and ON as parallel jobs and compares their exports in a third
# (#930 A2); locally both run here and are compared below.
modes="${OBS_TIER2_MODES:-OFF ON}"
for mode in $modes; do
  case "$mode" in OFF | ON) ;; *) echo "FAIL: OBS_TIER2_MODES must list OFF and/or ON, got '$modes'"; exit 1 ;; esac
done
has_mode() { grep -qw "$1" <<<"$modes"; }

for mode in $modes; do
  build="/build/rust-$mode"
  reset_stale_build "$build" "$rustc_version"
  echo "== [$mode] configure + build"
  cmake -S /src -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_UNIT_TESTS=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DENABLE_RUST_LIBOBS="$mode" \
    ${OBS_EXTRA_CFLAGS:+"-DCMAKE_C_FLAGS=$OBS_EXTRA_CFLAGS" "-DCMAKE_CXX_FLAGS=$OBS_EXTRA_CFLAGS"} \
    ${OBS_EXTRA_CMAKE_ARGS:-} \
    -DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF \
    -DENABLE_WEBSOCKET=OFF -DENABLE_AJA=OFF -DENABLE_WEBRTC=OFF \
    -DENABLE_NVENC=ON -DENABLE_QSV11=OFF -DENABLE_VST=OFF -DENABLE_WAYLAND=OFF \
    ${OBS_VERSION_OVERRIDE:+"-DOBS_VERSION_OVERRIDE=$OBS_VERSION_OVERRIDE"} \
    >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
  # -k 0: keep going after a failed target, so one run reports every compile
  # error (a warning ratchet otherwise took a round trip per file; #634).
  cmake --build "$build" --target libobs cmocka-tests decklink obs-nvenc -- -k 0 \
    >"$build.build.log" 2>&1 || { /src/rust/tools/linux-validate/build-errors.sh "$build.build.log"; exit 1; }
  if [ -n "${OBS_EXTRA_CFLAGS:-}" ]; then
    # Measuring extra flags: show what they report (#516).
    echo "== [$mode] warnings"
    grep -E ': warning: ' "$build.build.log" | sort -u || true
  fi

  # ctest starts the longest tests first only when it knows their past run
  # times (Testing/Temporary/CTestCostData.txt). A fresh CI container has
  # none, so the 83 s test_deinterlace_2x_field_order could start last; carry
  # the file per mode in the ccache directory, which CI restores (#931).
  cost_file="$build/Testing/Temporary/CTestCostData.txt"
  cost_cache=
  if [ -n "${CCACHE_DIR:-}" ]; then
    cost_cache="$CCACHE_DIR/ctest-cost-$mode.txt"
    if [ -s "$cost_cache" ]; then
      mkdir -p "${cost_file%/*}"
      cp "$cost_cache" "$cost_file"
    fi
  fi

  echo "== [$mode] ctest"
  # xvfb-run gives display tests an X server; the PipeWire tests start their
  # own private PipeWire daemon and D-Bus bus (#776).
  ctest_status=0
  xvfb-run -a ctest --test-dir "$build" --output-on-failure -j "${OBS_CTEST_JOBS:-1}" 2>&1 | tee "$build.ctest.log" ||
    ctest_status=$?
  # Keep the timings even when a test failed: they are still right.
  if [ -n "$cost_cache" ] && [ -s "$cost_file" ]; then
    cp "$cost_file" "$cost_cache"
  fi
  [ "$ctest_status" -eq 0 ] || exit "$ctest_status"
  # A PipeWire test that skips for lack of a daemon is a hole, not a pass (#776).
  /src/rust/tools/linux-validate/check-skips.sh "$build.ctest.log"

  if [ "$repeat" -gt 0 ]; then
    echo "== [$mode] ctest --repeat until-fail:$repeat${tests:+ -R $tests}"
    xvfb-run -a ctest --test-dir "$build" --output-on-failure \
      --repeat "until-fail:$repeat" ${tests:+-R "$tests"}
  fi

  lib=$(ls "$build"/libobs/libobs.so.* | head -1)
  nm -D --defined-only "$lib" | awk '{print $3}' | sort >"/build/exports-$mode.txt"
  # run.sh mounts OBS_TIER2_EXPORTS_DIR here so a CI job can hand the list on.
  if [ -d /exports ]; then
    cp "/build/exports-$mode.txt" /exports/
  fi

  # Query the current Ninja build graph, not the filesystem: the /build volume
  # persists across runs, so a stale .o from an older checkout would linger.
  libobs_objs=$(ninja -C "$build" -t inputs libobs | grep -E '\.c\.o$' || true)
  if [ "$mode" = ON ]; then
    for obj in $ported_objs; do
      if grep -q "libobs\.dir/$obj\$" <<<"$libobs_objs"; then
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

  # Issue #645: obs-nvenc swaps cuda-load.c for cuda-load-rust.c (#592).
  # Check the plugin links in both modes and takes the right loader.
  nvenc_objs=$(ninja -C "$build" -t inputs obs-nvenc 2>/dev/null | grep -E '\.c\.o$' || true)
  if [ "$mode" = ON ]; then want=cuda-load-rust.c.o not=cuda-load.c.o; else want=cuda-load.c.o not=cuda-load-rust.c.o; fi
  if ! grep -q "/$want\$" <<<"$nvenc_objs" || grep -q "/$not\$" <<<"$nvenc_objs"; then
    echo "FAIL: obs-nvenc with ENABLE_RUST_LIBOBS=$mode must compile $want and not $not"
    exit 1
  fi
  if [ -z "$(find "$build" -name 'obs-nvenc.so' | head -1)" ]; then
    echo "FAIL: obs-nvenc.so was not built with ENABLE_RUST_LIBOBS=$mode"
    exit 1
  fi
  echo "== [$mode] obs-nvenc.so built with $want"
done

# Issue #80: panic=abort is set only for [profile.release], so libobs must
# link the release-profile staticlib even for Debug builds; a dev-profile
# (panic=unwind) archive would let a shim panic unwind across the C ABI.
# Configure-only check: inspect which archive the Debug link line uses.
if has_mode ON; then
echo "== [ON/Debug] Rust staticlib profile"
dbg=/build/rust-ON-debug
reset_stale_build "$dbg" "$rustc_version"
cmake -S /src -B "$dbg" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_UNIT_TESTS=OFF -DENABLE_RUST_LIBOBS=ON \
  -DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF \
  -DENABLE_WEBSOCKET=OFF -DENABLE_AJA=OFF -DENABLE_WEBRTC=OFF \
  -DENABLE_NVENC=OFF -DENABLE_QSV11=OFF -DENABLE_VST=OFF -DENABLE_WAYLAND=OFF \
  ${OBS_VERSION_OVERRIDE:+"-DOBS_VERSION_OVERRIDE=$OBS_VERSION_OVERRIDE"} \
  >"$dbg.configure.log" 2>&1 || { tail -40 "$dbg.configure.log"; exit 1; }
rust_archives=$(grep -ohE '/cargo/build/[^ $]*/liblibobs_rust\.a' "$dbg/build.ninja" | sort -u)
echo "$rust_archives"
if [ -z "$rust_archives" ] || grep -qv '/release/liblibobs_rust\.a$' <<<"$rust_archives"; then
  echo "FAIL: Debug libobs links a non-release (panic=unwind) Rust staticlib"
  exit 1
fi
fi

if has_mode OFF && has_mode ON; then
  echo "== libobs exported symbols, OFF vs ON"
  diff /build/exports-OFF.txt /build/exports-ON.txt
  echo "IDENTICAL ($(wc -l </build/exports-ON.txt) symbols)"
fi

echo "== ccache"
ccache -s | grep -E "Hits|Misses|Cache size" || true
