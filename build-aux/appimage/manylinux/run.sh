#!/usr/bin/env bash
# Run a command inside the glibc 2.28 build image (build-aux/appimage/manylinux/
# Dockerfile, tagged obs-appimage-228 by the workflow), the way a workflow step
# would run it on the runner: the workspace and the runner's temp directory are
# mounted at their own paths (so GITHUB_ENV, GITHUB_OUTPUT and the ccache
# directory work unchanged), the step's environment is passed in, and the
# cargo registry is the runner's (restored and saved by actions/cache).
#   build-aux/appimage/manylinux/run.sh uv run build-aux/appimage/build.py --base manylinux --steps build
set -euo pipefail

IMAGE="${OBS_APPIMAGE_IMAGE:-obs-appimage-228}"
WORKSPACE="${GITHUB_WORKSPACE:-$PWD}"
TEMP="${RUNNER_TEMP:-/tmp}"
REGISTRY="${HOME}/.cargo/registry"
mkdir -p "$REGISTRY"

envs=()
while IFS= read -r name; do
    envs+=(-e "$name")
done < <(env | sed -n 's/^\(GITHUB_[A-Z_]*\|RUNNER_[A-Z_]*\|CCACHE_[A-Z_]*\|RUST_LIBOBS\|QSV\|OBS_RUST_VERSION\|PUBLISH_CONTINUOUS\|CI\)=.*/\1/p')

exec docker run --rm \
    -v "$WORKSPACE:$WORKSPACE" -v "$TEMP:$TEMP" -v "$REGISTRY:/usr/local/cargo/registry" \
    -w "$WORKSPACE" "${envs[@]}" "$IMAGE" "$@"
