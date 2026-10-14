#!/usr/bin/env bash
# Regression for #633. Run an ASan/LSan frontend build without an EGL vendor.
# Usage: test/video-init-failure.sh /absolute/path/to/obs /absolute/output/dir
# Requires Xvfb and xdotool. Logs and the isolated profile remain in output/.
set -euo pipefail
app=$(realpath "${1:?OBS executable required}")
out=$(realpath -m "${2:?output directory required}")
mkdir -p "$out/profile" "$out/runtime"
chmod 700 "$out/runtime"
app_pid=
xvfb_pid=
cleanup() {
  if [ -n "$app_pid" ] && kill -0 "$app_pid" 2>/dev/null; then
    kill "$app_pid" 2>/dev/null || true
    wait "$app_pid" 2>/dev/null || true
  fi
  if [ -n "$xvfb_pid" ]; then
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT
# -displayfd reports the allocated display only once Xvfb is ready.
coproc XVFB { exec Xvfb -displayfd 1 -screen 0 1280x720x24 2>"$out/xvfb.log"; }
xvfb_pid=$XVFB_PID
read -r display_number <&"${XVFB[0]}"
export DISPLAY=":$display_number"
export QT_QPA_PLATFORM=xcb XDG_CONFIG_HOME="$out/profile" XDG_RUNTIME_DIR="$out/runtime"
export __EGL_VENDOR_LIBRARY_FILENAMES="$out/no-egl-vendor.json"
export LD_LIBRARY_PATH="$(dirname "$app")/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ASAN_OPTIONS="detect_leaks=1:log_path=$out/asan"
export LSAN_OPTIONS="report_objects=1:exitcode=0"
(
  cd "$(dirname "$app")"
  exec "$app" --multi --disable-shutdown-check --disable-updater --verbose
) >"$out/app.log" 2>&1 &
app_pid=$!
window=$(timeout 90 xdotool search --sync --onlyvisible --name '^Error$' | head -1)
xdotool keydown --window "$window" Return
status=0
wait "$app_pid" || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 255 ]; then
  cat "$out/app.log"
  exit 1
fi
app_pid=
grep -q 'Failed to initialize video' "$out/app.log"
grep -qE 'eglInitialize.*failed|eglGet.*Display.*failed|Failed to get EGL display' "$out/app.log"
grep -q 'Number of memory leaks: 0' "$out/app.log" || {
  cat "$out/app.log"
  echo 'FAIL: video initialization failure leaked an OBS allocation' >&2
  exit 1
}
# The allocation check covers libobs ownership; LSan independently checks
# that the CPU sampler and frontend window were released on this path.
if grep -lE 'OBSBasic::OBSBasic|os_cpu_usage_info_start' "$out"/asan.* 2>/dev/null; then
  echo 'FAIL: LSan found a frontend initialization leak' >&2
  exit 1
fi
echo 'PASS: failed EGL initialization exits with zero OBS allocations'
