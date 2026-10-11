#!/bin/bash
# Installs what sanitize.sh needs on Ubuntu 26.04 (the CI runner): the OBS
# build dependencies from the Tier 2 image (rust/tools/linux-validate/Dockerfile,
# read from there so the lists can't drift) plus the tools for a headless launch.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
dockerfile=$here/../linux-validate/Dockerfile
# The package lines of the first apt-get install: after its line, up to the
# line that starts with &&.
pkgs=$(awk 'on && /^ *&&/ { exit } on { gsub(/\\/, ""); printf "%s ", $0 }
  /apt-get install -y --no-install-recommends/ { on = 1 }' "$dockerfile" | tr -s ' ')
[ -n "$pkgs" ] || { echo "no package list found in $dockerfile" >&2; exit 1; }
sudo=; [ "$(id -u)" = 0 ] || sudo=sudo
$sudo apt-get update
# shellcheck disable=SC2086 # word-splitting the package list is intended
$sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $pkgs \
  valgrind xvfb xauth libgl1-mesa-dri dbus-daemon dbus-x11 qt6-image-formats-plugins
