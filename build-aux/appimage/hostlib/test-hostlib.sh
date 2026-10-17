#!/usr/bin/env bash
# Tests the host-library loader (obs-rust/obs-studio#959) on a small AppDir:
# a program that links libva.so.2 and libva-drm.so.2 the way FFmpeg does goes
# through build.py, then runs with the host's libva, without it, with a bad
# candidate, with a copy inside the "AppImage", and through the preflight.
# Needs a C/C++ compiler, patchelf, binutils, uv, libva-devel and libEGL.
#   test-hostlib.sh
set -euo pipefail

HERE="$(dirname "$(realpath "$0")")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
fails=0
pass() { echo "PASS $*"; }
fail() {
    echo "FAIL $*"
    fails=$((fails + 1))
}

app="$work/AppDir"
mkdir -p "$app/usr/bin" "$app/usr/lib"
cat >"$work/probe.c" <<'EOF'
#include <stdio.h>
#include <va/va.h>
#include <va/va_drm.h>
int main(void)
{
	/* No device needed: a bad fd gives a NULL display from the real libva too,
	 * so this probes loading and the fallback, not the GPU. */
	VADisplay d = vaGetDisplayDRM(-1);
	int major = 0, minor = 0;
	VAStatus s = d ? vaInitialize(d, &major, &minor) : VA_STATUS_ERROR_INVALID_DISPLAY;
	/* vaCreateSurfaces is versioned (VA_API_0.33.0): the shim must define that version. */
	if (d && s == VA_STATUS_SUCCESS)
		vaCreateSurfaces(d, VA_RT_FORMAT_YUV420, 16, 16, NULL, 0, NULL, 0);
	printf("display=%s status=0x%x str=%s\n", d ? "yes" : "null", s, vaErrorStr(VA_STATUS_ERROR_UNIMPLEMENTED));
	return 0;
}
EOF
cc -o "$app/usr/bin/probe" "$work/probe.c" -lva -lva-drm -Wl,-rpath,'$ORIGIN/../lib'

# --- build ------------------------------------------------------------------
if out="$(uv run --no-project "$HERE/build.py" "$app" 2>&1)"; then
    pass "build.py"
else
    fail "build.py: $out"
    exit 1
fi
echo "$out"
needed="$(patchelf --print-needed "$app/usr/bin/probe")"
grep -qx libobs-va.so.2 <<<"$needed" && grep -qx libobs-va-drm.so.2 <<<"$needed" &&
    ! grep -qx libva.so.2 <<<"$needed" && pass "probe links the shims, not libva" ||
    fail "probe's DT_NEEDED: $needed"
[ -x "$app/usr/bin/obs-hostlib-check" ] && pass "obs-hostlib-check built" || fail "no obs-hostlib-check"

probe() { APPDIR="$app" "$app/usr/bin/probe" 2>&1; }

# --- 1. the host's libva ------------------------------------------------------
out="$(probe)"
grep -q "no version information" <<<"$out" && fail "loader warns about the shim's versions: $out" ||
    pass "no version warning from the loader"
grep -q 'hostlib: va -> /' <<<"$out" && pass "host libva found" || fail "host libva: $out"
grep -q '/proc\|libobs-va' <<<"$(APPDIR="$app" LD_DEBUG=libs "$app/usr/bin/probe" 2>&1)" || true
grep -q 'str=.*unimplemented\|str=the requested function is not implemented' <<<"$out" &&
    pass "the real vaErrorStr answered" || fail "vaErrorStr with host libva: $out"

# --- 2. no libva: the stubs ---------------------------------------------------
out="$(OBS_HOSTLIB_VA=/nonexistent/libva.so.2 OBS_HOSTLIB_VA_DRM=/nonexistent/libva-drm.so.2 probe)"
grep -q 'hostlib: va unavailable' <<<"$out" && pass "missing libva reported" || fail "missing libva: $out"
grep -q 'display=null' <<<"$out" && pass "vaGetDisplayDRM stub returns NULL" || fail "stub display: $out"
grep -q 'str=(null)' <<<"$out" && pass "vaErrorStr stub returns NULL" || fail "stub vaErrorStr: $out"

# --- 3. a candidate that does not load (wrong ELF class) ----------------------
printf '\x7fELF\x01\x01\x01' >"$work/libva.so.2" # a truncated 32-bit header
out="$(OBS_HOSTLIB_VA="$work/libva.so.2" OBS_HOSTLIB_DEBUG=1 probe)"
grep -q "$work/libva.so.2: does not load" <<<"$out" && pass "bad candidate rejected with reason" ||
    fail "bad candidate: $out"

# --- 4. a copy inside the AppImage is refused ----------------------------------
real="$(ldconfig -p | sed -n 's/.*libva.so.2 (libc6,x86-64) => //p' | head -1)"
cp -L "$real" "$app/usr/lib/libva.so.2"
out="$(OBS_HOSTLIB_VA="$app/usr/lib/libva.so.2" OBS_HOSTLIB_DEBUG=1 probe)"
grep -q 'is inside the AppImage' <<<"$out" && pass "AppImage copy refused" || fail "AppImage copy: $out"
rm "$app/usr/lib/libva.so.2"

# --- 5. a library without the required symbols ---------------------------------
echo 'int vaUnrelated(void) { return 0; }' >"$work/old.c"
cc -shared -fPIC -o "$work/libva-old.so.2" "$work/old.c"
out="$(OBS_HOSTLIB_VA="$work/libva-old.so.2" OBS_HOSTLIB_DEBUG=1 probe)"
grep -q 'lacks vaInitialize' <<<"$out" && pass "too-old candidate rejected" || fail "old candidate: $out"

# --- 6. preflight -------------------------------------------------------------
if APPDIR="$app" "$app/usr/bin/obs-hostlib-check"; then pass "preflight passes with libEGL"; else fail "preflight failed"; fi
set +e
out="$(OBS_HOSTLIB_EGL=/nonexistent/libEGL.so.1 APPDIR="$app" "$app/usr/bin/obs-hostlib-check" 2>&1)"
rc=$?
set -e
[ "$rc" = 127 ] && grep -q 'libEGL.so.1' <<<"$out" && pass "missing libEGL: exit 127, named" ||
    fail "preflight without libEGL (rc $rc): $out"

[ "$fails" = 0 ] && echo "test-hostlib: all passed" || {
    echo "test-hostlib: $fails failed"
    exit 1
}
