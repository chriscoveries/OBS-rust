#!/usr/bin/env bash
# Host driver search paths that AppRun hands to OBS (obs-rust/obs-studio#722).
#
# On NixOS the GPU driver closure lives under /run/opengl-driver, where no
# library looks by default. Without these variables NVIDIA on NixOS Wayland
# silently fell back to Mesa llvmpipe. The test reads the environment of the
# running obs process (/proc/<pid>/environ), which is what its EGL, GBM, VA-API
# and Vulkan loaders actually see.
#
#   test-apprun-driver-env.sh <AppRun>
#       Runs <AppRun> (from the source tree or an extracted AppImage) with a
#       stub obs, each case in its own user and mount namespace with an empty
#       tmpfs on /run, and checks:
#         nixos: a fake NixOS driver tree exists; every variable lists its
#                /run/opengl-driver directory first, then the standard
#                directories that exist on this host, and nothing else.
#         none:  no /run/opengl-driver; every variable lists exactly the
#                standard directories that exist, or is unset if none does.
#         user:  every variable is preset by the user and reaches obs unchanged.
#   test-apprun-driver-env.sh --check <pid> <nixos|none>
#       Only the check, against a running obs process (test-appimage.sh uses
#       this for the real packaged OBS).
set -euo pipefail

# name|NixOS directory|standard directories (same order as AppRun's search)
DRIVER_VARS=(
    "GBM_BACKENDS_PATH|/run/opengl-driver/lib/gbm|/usr/lib/x86_64-linux-gnu/gbm /usr/lib64/gbm /usr/lib/gbm"
    "LIBVA_DRIVERS_PATH|/run/opengl-driver/lib/dri|/usr/lib/x86_64-linux-gnu/dri /usr/lib64/dri /usr/lib/dri"
    "__EGL_VENDOR_LIBRARY_DIRS|/run/opengl-driver/share/glvnd/egl_vendor.d|/etc/glvnd/egl_vendor.d /usr/share/glvnd/egl_vendor.d"
    "__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS|/run/opengl-driver/share/egl/egl_external_platform.d|/etc/egl/egl_external_platform.d /usr/share/egl/egl_external_platform.d"
    "VK_DRIVER_FILES|/run/opengl-driver/share/vulkan/icd.d|/etc/vulkan/icd.d /usr/share/vulkan/icd.d"
)

# A NixOS driver tree with the NVIDIA EGL Wayland platform that went missing.
make_nixos_tree() {
    local entry
    for entry in "${DRIVER_VARS[@]}"; do
        IFS='|' read -r _ nix _ <<<"$entry"
        mkdir -p "$nix"
    done
    cat >/run/opengl-driver/share/egl/egl_external_platform.d/10_nvidia_wayland.json <<'EOF'
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libnvidia-egl-wayland.so.1"
    }
}
EOF
}

# The value of variable $2 in /proc/$1/environ, and whether it is set.
env_of() {
    local line
    while IFS= read -r -d '' line; do
        if [ "${line%%=*}" = "$2" ]; then
            printf 'set:%s' "${line#*=}"
            return
        fi
    done <"/proc/$1/environ"
    printf 'unset'
}

# check <pid> <nixos|none|user>: prints PASS/FAIL lines, returns 1 on a failure.
check() {
    local pid="$1" kind="$2" rc=0 entry name nix std d want got
    for entry in "${DRIVER_VARS[@]}"; do
        IFS='|' read -r name nix std <<<"$entry"
        want=""
        case "$kind" in
        user) want="/user/selected/$name" ;;
        *)
            [ "$kind" = nixos ] && want="$nix"
            for d in $std; do
                [ -d "$d" ] && want="${want:+$want:}$d"
            done
            ;;
        esac
        if [ -n "$want" ]; then want="set:$want"; else want=unset; fi
        got="$(env_of "$pid" "$name")"
        if [ "$got" = "$want" ]; then
            echo "PASS [$kind] $name: $got"
        else
            echo "FAIL [$kind] $name: expected $want, obs has $got"
            rc=1
        fi
    done
    return "$rc"
}

if [ "${1:-}" = --check ]; then
    check "${2:?pid}" "${3:?nixos|none}"
    exit
fi
if [ "${1:-}" = --in-namespace ]; then
    # Internal: one case, inside `unshare -rm`. $2: AppDir, $3: case.
    appdir="$2" kind="$3"
    # A private /run without the host's opengl-driver (a real NixOS host has
    # one), but with everything else in it, since there may be PATH there
    # (/run/current-system, /run/wrappers).
    # The new /run is assembled beside the old one and moved over it last.
    newrun="$appdir/../newrun"
    mkdir -p "$newrun"
    mount -t tmpfs none "$newrun"
    for e in /run/* /run/.[!.]*; do
        [ -e "$e" ] || [ -L "$e" ] || continue
        b="${e##*/}"
        [ "$b" = opengl-driver ] && continue
        if [ -L "$e" ]; then
            ln -s "$(readlink "$e")" "$newrun/$b"
        elif [ -d "$e" ]; then
            mkdir "$newrun/$b" && mount --rbind "$e" "$newrun/$b"
        else
            touch "$newrun/$b" && mount --bind "$e" "$newrun/$b"
        fi
    done
    mount --move "$newrun" /run
    [ "$kind" = nixos ] && make_nixos_tree
    if [ "$kind" = user ]; then
        for entry in "${DRIVER_VARS[@]}"; do
            IFS='|' read -r name _ <<<"$entry"
            export "$name=/user/selected/$name"
        done
        make_nixos_tree # a user's choice wins even on NixOS
    else
        unset GBM_BACKENDS_PATH LIBVA_DRIVERS_PATH __EGL_VENDOR_LIBRARY_DIRS __EGL_VENDOR_LIBRARY_FILENAMES \
            __EGL_EXTERNAL_PLATFORM_CONFIG_DIRS __EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES VK_DRIVER_FILES VK_ICD_FILENAMES
    fi
    "$appdir/AppRun" >"$appdir/apprun-$kind.log" 2>&1 &
    apprun=$!
    obs=""
    for _ in $(seq 1 100); do
        obs="$(pgrep -P "$apprun" || true)"
        [ -n "$obs" ] && break
        sleep 0.1
    done
    if [ -z "$obs" ]; then
        echo "FAIL [$kind] AppRun never started obs:"
        cat "$appdir/apprun-$kind.log"
        kill "$apprun" 2>/dev/null || true
        exit 1
    fi
    rc=0
    check "$obs" "$kind" || rc=1
    kill "$obs" 2>/dev/null || true
    wait "$apprun" 2>/dev/null || true
    exit "$rc"
fi

APPRUN="$(realpath "${1:?usage: test-apprun-driver-env.sh <AppRun> | --check <pid> <nixos|none>}")"
SELF="$(realpath "$0")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
# A minimal AppDir around AppRun: a private loader directory, a C runtime that
# any host counts as older than its own, and an obs that only waits.
APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/glibc" "$APPDIR/lib64" "$WORK/loader"
sed -e "s|^LOADER=.*|LOADER=\"$WORK/loader/ld.so\"|" -e 's|@OBS_GUI_NAME@|obs|g' "$APPRUN" >"$APPDIR/AppRun"
chmod 755 "$APPDIR/AppRun" "$WORK/loader"
echo 2.0 >"$APPDIR/glibc/GLIBC_VERSION"
echo 0 >"$APPDIR/glibc/GLIBCXX_MINOR"
: >"$APPDIR/glibc/placeholder.so"
for c in /lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 /usr/lib/ld-linux-x86-64.so.2; do
    [ -e "$c" ] && { cp "$(readlink -f "$c")" "$APPDIR/lib64/ld-linux-x86-64.so.2"; break; }
done
printf '#!/bin/sh\nexec sleep 60\n' >"$APPDIR/usr/bin/obs"
chmod 755 "$APPDIR/usr/bin/obs"

echo "AppRun under test: $APPRUN"
FAILED=0
for kind in nixos none user; do
    unshare -rm bash "$SELF" --in-namespace "$APPDIR" "$kind" || FAILED=1
done
if [ "$FAILED" = 1 ]; then
    echo "AppRun does not hand OBS the host driver search paths it should (see FAIL above)"
    exit 1
fi
echo "AppRun hands OBS the host driver search paths"
