#!/bin/sh
# Usage: install-zccache.sh [DEST_DIR]
# Installs the pinned zccache release (static musl build) into DEST_DIR
# (default ~/.local/bin) after checking its SHA-256. It is the compiler
# launcher of every Linux CMake build here: Tier 2 (the Dockerfile next to
# this script), the Ubuntu build, the AppImage and the sanitizers. soldr
# 0.9.35 embeds the same zccache for rustc but exposes no C launcher.
set -eu
ZCCACHE_VERSION="1.15.4"
dest=${1:-"$HOME/.local/bin"}
case "$(uname -m)" in
x86_64 | amd64)
  arch=x86_64
  sha=51c0ef8380cc6259a150cf561dd8d1a4eb6f8f5fe8212929a2cd1c71e6937853
  ;;
aarch64 | arm64)
  arch=aarch64
  sha=95828e476d9a47ca8c5a8a88bd3c3bf094f8ab52391a911566aba7379ae8b9d4
  ;;
*)
  echo "install-zccache.sh: unsupported arch $(uname -m)" >&2
  exit 1
  ;;
esac
if [ -x "$dest/zccache" ] && "$dest/zccache" --version 2>/dev/null | grep -qx "zccache $ZCCACHE_VERSION"; then
  exit 0
fi
name="zccache-v$ZCCACHE_VERSION-$arch-unknown-linux-musl"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL --retry 3 -o "$tmp/$name.tar.gz" \
  "https://github.com/zackees/zccache/releases/download/$ZCCACHE_VERSION/$name.tar.gz"
(cd "$tmp" && echo "$sha  $name.tar.gz" | sha256sum -c - >/dev/null)
tar -xzf "$tmp/$name.tar.gz" -C "$tmp"
mkdir -p "$dest"
install -m 0755 "$tmp/$name/zccache" "$dest/zccache"
"$dest/zccache" --version
