#!/usr/bin/env -S uv run --script
"""Canonical AppImage names and updater metadata (#624).

SHA-1 is the zsync format's whole-file checksum, not a signature.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
from pathlib import Path


def asset_name(version_output: str) -> str:
    version_output = version_output.strip()
    rust = re.fullmatch(r"OBS-Studio-Rust ([^ ]+) \(based on OBS .+\)", version_output)
    upstream = re.fullmatch(r"OBS Studio - ([^ ]+)", version_output)
    match = rust or upstream
    if not match or not re.fullmatch(r"[0-9][A-Za-z0-9.+-]*", match[1]):
        raise ValueError(f"unrecognized executable version: {version_output!r}")
    product = "OBS-Studio-Rust" if rust else "OBS-Studio"
    return f"{product}-{match[1]}-linux-x86_64.AppImage"


def check_metadata(
    appimage: Path,
    version_output: str,
    update_info: str,
    repository: str,
    require_update: bool = False,
) -> None:
    if appimage.name != asset_name(version_output):
        raise ValueError("AppImage filename differs from its executable identity")
    update_info = update_info.strip()
    if not update_info and not require_update:
        return
    owner, repo = repository.split("/")
    expected = f"gh-releases-zsync|{owner}|{repo}|continuous|OBS-Studio-Rust-*-linux-x86_64.AppImage.zsync"
    if update_info != expected:
        raise ValueError(f"incorrect embedded update target: {update_info!r}")
    sidecar = Path(str(appimage) + ".zsync")
    with sidecar.open("rb") as stream:
        header = stream.read(65536).split(b"\n\n", 1)[0].decode("utf-8")
    fields: dict[str, str] = {}
    for line in header.splitlines():
        key, value = line.split(":", 1)
        if key in fields:
            raise ValueError(f"duplicate zsync header field: {key}")
        fields[key] = value.strip()
    if fields.get("Filename") != appimage.name:
        raise ValueError("zsync Filename does not name the AppImage")
    download = (
        f"https://github.com/{repository}/releases/download/continuous/{appimage.name}"
    )
    if fields.get("URL") not in (appimage.name, download):
        raise ValueError("zsync URL does not name the AppImage")
    if fields.get("Length") != str(appimage.stat().st_size):
        raise ValueError("zsync Length does not match the AppImage")
    with appimage.open("rb") as stream:
        checksum = hashlib.file_digest(
            stream, lambda: hashlib.sha1(usedforsecurity=False)
        ).hexdigest()
    if fields.get("SHA-1") != checksum:
        raise ValueError("zsync SHA-1 does not match the AppImage")


def runtime_output(appimage: Path, option: str, *, extract: bool = False) -> str:
    environment = dict(os.environ)
    environment.pop("APPIMAGE_EXTRACT_AND_RUN", None)
    if extract:
        environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    try:
        return subprocess.run(
            [str(appimage), option],
            env=environment,
            check=True,
            capture_output=True,
            text=True,
            timeout=60,
        ).stdout
    except subprocess.CalledProcessError as error:
        diagnostic = (error.stderr or error.stdout or "no runtime diagnostic")[-8192:]
        raise RuntimeError(
            f"AppImage {option} failed ({error.returncode}): {diagnostic}"
        ) from error


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    name = commands.add_parser("name")
    name.add_argument("--version", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("appimage", type=Path)
    verify.add_argument("--repository", required=True)
    verify.add_argument("--require-update", action="store_true")
    args = parser.parse_args()
    if args.command == "name":
        print(asset_name(args.version))
        return
    appimage = args.appimage.resolve(strict=True)
    version = runtime_output(appimage, "--version", extract=True)
    update = runtime_output(appimage, "--appimage-updateinformation")
    check_metadata(appimage, version, update, args.repository, args.require_update)
    print(f"AppImage identity and updater metadata verified: {appimage.name}")


if __name__ == "__main__":
    main()
