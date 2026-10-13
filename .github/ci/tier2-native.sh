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

# A ctest failure doesn't stop the run: the other mode, the export dump and
# the OFF/ON export diff still run, so one flaky test can't hide the rest.
# The script exits with the first ctest failure code at the end.
ctest_rc=0
for mode in OFF ON; do
  build="$root/rust-$mode"
  echo "== [$mode] configure + build"
  # The persistent build dir is shared by every branch. UseSWIG tracks only
  # the .i files, not the libobs headers they include, so the generated
  # Lua/Python wrappers of test_scripting_obs_defs go stale when a branch adds
  # or removes a graphics/obs function. Touch the .i files so SWIG always
  # regenerates them.
  touch "$src/shared/obs-scripting/obslua/obslua.i" \
    "$src/shared/obs-scripting/obspython/obspython.i"
  cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_UNIT_TESTS=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DENABLE_RUST_LIBOBS="$mode" "${flags[@]}" \
    >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
  cmake --build "$build" --target libobs cmocka-tests decklink \
    >"$build.build.log" 2>&1 || { grep -E "error|Error" "$build.build.log" | head -40; exit 1; }

  # Three passes. Most tests run ${CTEST_JOBS:-4}-wide. Tests that measure
  # timing or drive real playback/encoding mostly wait on real time (about
  # 270 s of sleeps in total, the longest about 75 s), so they run 3-wide after
  # the CPU-heavy pass. Tests that share a desktop session service (the D-Bus
  # portal, PipeWire, Wayland) run strictly one at a time: two portal tests at
  # once collide on the shared session.
  timed='clock|drift|sync|deinterlace|vlc|ffmpeg|media|transition|v4l2|restart'
  exclusive='portal|pipewire|wayland'
  ct=($xvfb ctest --test-dir "$build" --output-on-failure --timeout 600)
  echo "== [$mode] ctest: -j${CTEST_JOBS:-4}, timed -j${CTEST_TIMED_JOBS:-3}, session tests serial${xvfb:+ (under xvfb-run)}"
  rc=0
  # A crashing test leaves a core file (claw's core_pattern is "core", written
  # to the test's working directory); print its backtrace so a rare crash is
  # diagnosable from the log alone.
  ulimit -c unlimited 2>/dev/null || true
  echo "== [$mode] core file limit: $(ulimit -c)"
  touch "$build/.ctest-start"
  "${ct[@]}" -j "${CTEST_JOBS:-4}" -E "$timed|$exclusive" || rc=$?
  # Timed tests are sensitive to load on this shared runner, so each gets one
  # retry. A test that needed the retry is printed as FLAKY, so a PR body can
  # report it; a test that fails twice still fails the run.
  timed_log="$build/ctest-timed.log"
  timed_rc=0
  "${ct[@]}" -j "${CTEST_TIMED_JOBS:-3}" -R "$timed" -E "$exclusive" \
    --repeat until-pass:2 2>&1 | tee "$timed_log" || timed_rc=$?
  [ "$timed_rc" -eq 0 ] || rc=$timed_rc
  flaky=$(grep -oE 'Test +#[0-9]+: [A-Za-z0-9_]+ \.+\*\*\*(Failed|Timeout|Exception)' "$timed_log" |
    awk '{print $3}' | sort -u | tr '\n' ' ' || true)
  if [ -n "$flaky" ]; then
    if [ "$timed_rc" -eq 0 ]; then
      echo "== [$mode] FLAKY (failed once, passed on retry): $flaky"
    else
      echo "== [$mode] FAILED in the timed pass (a test failed both attempts); tests with a failed attempt: $flaky"
    fi
  fi
  "${ct[@]}" -R "$exclusive" || rc=$?
  while IFS= read -r core; do
    exe=$(gdb -batch -c "$core" 2>/dev/null | sed -n "s/^Core was generated by \`\([^ ']*\).*/\1/p")
    echo "== [$mode] core $core (${exe:-unknown executable})"
    [ -n "$exe" ] && gdb -batch -ex 'thread apply all bt' "$exe" "$core" 2>&1 | tail -200 || true
    rm -f "$core"
  done < <(find "$build" -xdev -type f \( -name core -o -name 'core.[0-9]*' \) -newer "$build/.ctest-start" 2>/dev/null)
  if [ "$rc" -ne 0 ]; then
    echo "== [$mode] ctest FAILED (rc=$rc); continuing with the remaining checks"
    [ "$ctest_rc" -ne 0 ] || ctest_rc=$rc
  fi
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

if [ "$ctest_rc" -ne 0 ]; then
  echo "== ctest failed in at least one mode (rc=$ctest_rc); see the FAILED lines above"
  exit "$ctest_rc"
fi
