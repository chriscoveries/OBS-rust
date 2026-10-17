#!/usr/bin/env -S uv run --script
"""Fetch, verify and unpack one obs-rust/obs-studio-libs release asset (#992).

    uv run ci/fetch_libs_asset.py TAG ASSET SHA256 DEST

Skips everything when DEST/.asset-sha256 already holds SHA256 (a cache hit).
The archive is deleted once unpacked; pkg-config files that name the build-time
prefix /opt/obs-deps/<name> are rewritten to DEST so PKG_CONFIG_PATH works.
"""

from __future__ import annotations

import hashlib
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

URL = "https://github.com/obs-rust/obs-studio-libs/releases/download/{tag}/{asset}"
STAMP = ".asset-sha256"


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def rewrite_pkgconfig(dest: Path) -> int:
    n = 0
    for pc in dest.rglob("*.pc"):
        text = pc.read_text()
        new = re.sub(r"/opt/obs-deps/[A-Za-z0-9_.+-]+", str(dest), text)
        if new != text:
            pc.write_text(new)
            n += 1
    return n


def fetch(tag: str, asset: str, sha: str, dest: Path) -> None:
    sha = sha.lower()
    if (dest / STAMP).is_file() and (dest / STAMP).read_text().strip() == sha:
        print(f"{asset}: cache hit ({dest})")
        return
    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / asset
        urllib.request.urlretrieve(URL.format(tag=tag, asset=asset), archive)
        got = sha256_of(archive)
        if got != sha:
            raise SystemExit(f"{asset}: sha256 {got} != pinned {sha}")
        dest.mkdir(parents=True, exist_ok=True)
        subprocess.run(["tar", "--zstd", "-xf", str(archive), "-C", str(dest)], check=True)
    print(f"{asset}: rewrote {rewrite_pkgconfig(dest)} pkg-config files")
    (dest / STAMP).write_text(sha + "\n")


def main() -> int:
    if len(sys.argv) != 5 or not re.fullmatch(r"[0-9a-fA-F]{64}", sys.argv[3]):
        print(__doc__, file=sys.stderr)
        return 2
    fetch(sys.argv[1], sys.argv[2], sys.argv[3], Path(sys.argv[4]).resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
