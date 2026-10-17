#!/usr/bin/env bash
# Fast-path Linux Qt frontend build (obs-rust/obs-studio#992).
# Configures and links the OBS Qt frontend (the obs-studio target) against the
# PREBUILT Qt prefix from obs-studio-libs (never compiles Qt), then builds and
# runs the Qt-linked cmocka tests (tests.d fragments that find Qt6).
#
#   ci/linux_qt_frontend.sh QT_PREFIX FFMPEG_PREFIX BUILD_DIR
#
# Env: CCACHE_DIR (optional), CTEST_JOBS (default 4).
set -euo pipefail
[ $# -eq 3 ] || { echo "usage: linux_qt_frontend.sh QT_PREFIX FFMPEG_PREFIX BUILD_DIR" >&2; exit 2; }
qt=$(realpath "$1")
ff=$(realpath "$2")
build=$3
src=$(cd "$(dirname "$0")/.." && pwd)

[ -f "$qt/lib/cmake/Qt6/Qt6Config.cmake" ] || { echo "no Qt6 in $qt" >&2; exit 1; }

export PKG_CONFIG_PATH="$ff/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$ff/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -S "$src" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$qt;$ff" -DQt6_DIR="$qt/lib/cmake/Qt6" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DENABLE_UNIT_TESTS=ON -DENABLE_RUST_LIBOBS=OFF \
  -DENABLE_FRONTEND=ON -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF \
  -DENABLE_WEBSOCKET=OFF -DENABLE_AJA=OFF -DENABLE_WEBRTC=OFF \
  -DENABLE_NVENC=OFF -DENABLE_QSV11=OFF -DENABLE_VST=OFF -DENABLE_WAYLAND=OFF \
  -DENABLE_VLC=OFF -DENABLE_NEW_MPEGTS_OUTPUT=OFF -DENABLE_NATIVE_NVENC=OFF \
  2>&1 | tee "$build.configure.log"

# The distro Qt must not be what got linked (#992).
grep -E "^-- Found Qt6|Qt6_DIR" "$build.configure.log" || true
if ! grep -q "^Qt6Widgets_DIR:PATH=$qt/" "$build/CMakeCache.txt"; then
  echo "Qt6Widgets_DIR is not in the prebuilt prefix:" >&2
  grep "^Qt6.*_DIR" "$build/CMakeCache.txt" >&2 || true
  exit 1
fi

grep -iE "FFmpeg|avcodec" "$build.configure.log" | head -5 || true
pc_prefix=$(pkg-config --variable=prefix libavcodec)
[ "$pc_prefix" = "$ff" ] || { echo "libavcodec is not from $ff ($pc_prefix)" >&2; exit 1; }
pkg-config --modversion libavcodec

# Qt-linked tests: tests.d fragments that find Qt6.
tests=$(grep -l "Qt6" "$src"/test/cmocka/tests.d/*.cmake | xargs -n1 basename | sed 's/\.cmake$//' | tr '\n' ' ')
# A fragment can skip its target when a Qt module is missing from the prebuilt
# archive (Qt6::Test is: test_hotkey_capture_mnemonic is not built, #992).
have=$(ninja -C "$build" -t targets all | sed -n 's/^\([A-Za-z0-9_]*\): phony$/\1/p;s/^\([A-Za-z0-9_]*\): CXX_EXECUTABLE_LINKER.*/\1/p')
built=""
for t in $tests; do
  if grep -qx "$t" <<<"$have"; then built="$built $t"; else echo "== skipped (no target, missing Qt module?): $t"; fi
done
tests=${built# }
echo "== Qt-linked tests: $tests"

# shellcheck disable=SC2086
ninja -C "$build" obs-studio $tests

export LD_LIBRARY_PATH="$qt/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$qt/plugins"
regex="^($(echo $tests | sed 's/ /|/g'))\$"
xvfb-run -a ctest --test-dir "$build" --output-on-failure -j "${CTEST_JOBS:-4}" -R "$regex"
