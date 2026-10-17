#!/usr/bin/env bash
# NixOS lane of the release gate (obs-rust/obs-studio#926): run the packaged
# AppImage the way NixOS users do, through nixpkgs' appimageTools.wrapType2
# (no FHS, no driver-path workaround), under Xvfb, and check that OBS starts:
# the host-library preflight passes, OBS reaches its main window and keeps
# running, and VA-API is reported unavailable rather than stopping the start
# (NixOS has no libva where the loader looks; #959).
# Runs inside the nixos/nix image; nothing is compiled here except Nix's
# own wrapper around the already-built AppImage.
#   test-appimage-nixos.sh <AppImage> <out-dir>
set -euo pipefail

APPIMAGE="$(realpath "$1")"
OUT="$2"
mkdir -p "$OUT"
# The nixpkgs the wrapper and the test tools come from (the revision a current
# NixOS 26.05 system runs).
NIXPKGS="github:NixOS/nixpkgs/c5c4a43b0e8056328ec4529f735cabdb8f1942bb"
nix=(nix --extra-experimental-features "nix-command flakes")

echo "::group::Wrap the AppImage with appimageTools.wrapType2"
"${nix[@]}" build --impure -o /tmp/obs --expr "
  (builtins.getFlake \"$NIXPKGS\").legacyPackages.x86_64-linux.appimageTools.wrapType2 {
    pname = \"obs-studio-rust\"; version = \"release-gate\"; src = $APPIMAGE;
  }"
ls /tmp/obs/bin
echo "::endgroup::"

fail=0
error() {
    echo "::error::$*"
    fail=1
}

echo "::group::Graphics driver the NixOS way (/run/opengl-driver)"
# NixOS's hardware.graphics puts the driver closure at /run/opengl-driver;
# AppRun points EGL, GBM, VA-API and Vulkan there (#722). No GPU here: Mesa's
# software rasterizer, as the other lanes use.
mesa="$("${nix[@]}" build --no-link --print-out-paths "$NIXPKGS#mesa")"
mkdir -p /run
ln -sfn "$mesa" /run/opengl-driver
ls /run/opengl-driver/share/glvnd/egl_vendor.d
export LIBGL_ALWAYS_SOFTWARE=1
echo "::endgroup::"

echo "::group::Start OBS under Xvfb"
export DISPLAY=:99 XDG_RUNTIME_DIR=/tmp/xdg HOME=/tmp/home
mkdir -p "$XDG_RUNTIME_DIR" "$HOME"
chmod 700 "$XDG_RUNTIME_DIR"
"${nix[@]}" shell "$NIXPKGS#xorg.xorgserver" "$NIXPKGS#dbus" "$NIXPKGS#procps" "$NIXPKGS#coreutils" --command bash -c '
    Xvfb :99 -screen 0 1280x720x24 >/dev/null 2>&1 &
    xvfb=$!
    sleep 3
    dbus-run-session --config-file="$(dirname "$(command -v dbus-daemon)")/../share/dbus-1/session.conf" -- /tmp/obs/bin/obs-studio-rust --verbose --disable-shutdown-check >"'"$OUT"'/obs-stdout.log" 2>&1 &
    obs=$!
    sleep 45
    if kill -0 "$obs" 2>/dev/null; then echo running >"'"$OUT"'/state"; else wait "$obs"; echo "exited $?" >"'"$OUT"'/state"; fi
    pkill -TERM -f "[u]sr/bin/obs-studio-rust-gui" 2>/dev/null || true
    sleep 5
    kill "$xvfb" 2>/dev/null || true
'
echo "::endgroup::"

log="$OUT/obs-stdout.log"
state="$(cat "$OUT/state" 2>/dev/null || echo unknown)"
echo "OBS after 45 s: $state"
[ "$state" = running ] || error "OBS did not keep running on NixOS ($state); see $log"
grep -q "cannot start\|needs these libraries from your system" "$log" &&
    error "the host-library preflight refused to start OBS on NixOS"
grep -qE "Switched to scene|Loaded scenes|Created scene collection" "$log" ||
    error "OBS did not reach its main window"
grep -qE "Loading up OpenGL on adapter" "$log" || error "OBS did not initialize OpenGL"
grep -E "hostlib: va" "$log" || true
grep -E "Loading up OpenGL on adapter|OBS-Studio-Rust [0-9]" "$log" | head -3 || true
if grep -qE "error while loading shared libraries|Segmentation fault|SIGSEGV" "$log"; then
    error "OBS crashed or could not load its libraries on NixOS"
fi

if [ "$fail" = 0 ]; then
    echo "NixOS: OBS AppImage started through appimageTools.wrapType2"
else
    tail -40 "$log" || true
    exit 1
fi
