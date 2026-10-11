#!/bin/bash
# Sourced by validate.sh. The /build volume outlives image rebuilds, and
# Corrosion caches the configuring toolchain's rustc and cargo paths. After
# the rust-toolchain.toml pin changes, the image no longer has those paths
# and reconfiguring an old build dir fails with "Could not find toolchain".

# reset_stale_build BUILD_DIR RUSTC_VERSION
# Removes BUILD_DIR unless BUILD_DIR.rustc records RUSTC_VERSION, then
# records it. ccache keeps the C rebuild warm.
reset_stale_build() {
  local build=$1 version=$2
  if [ "$(cat "$build.rustc" 2>/dev/null)" != "$version" ]; then
    rm -rf "$build"
  fi
  printf '%s\n' "$version" >"$build.rustc"
}
