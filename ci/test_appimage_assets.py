#!/usr/bin/env -S uv run --script
"""Canonical release assets and real updater metadata contracts for #624."""

import hashlib
import os
import importlib.util
import tempfile
import subprocess
import unittest
import textwrap
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "asset_metadata", ROOT / "build-aux/appimage/asset_metadata.py"
)
metadata = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(metadata)
VERSION = "OBS-Studio-Rust 0.1.0-beta.20261014 (based on OBS 33.0.0)"
NAME = "OBS-Studio-Rust-0.1.0-beta.20261014-linux-x86_64.AppImage"
TARGET = "gh-releases-zsync|obs-rust|obs-studio|continuous|OBS-Studio-Rust-*-linux-x86_64.AppImage.zsync"


class AppImageAssetsTest(unittest.TestCase):
    maxDiff = 250

    def test_metadata_host_installs_driver_libraries_before_launch(self):
        workflow = (ROOT / ".github/workflows/appimage.yaml").read_text()
        install = workflow.find("- name: Install host GPU runtime for metadata check")
        verify = workflow.find("- name: Verify AppImage identity and updater metadata")
        self.assertGreater(
            install,
            0,
            "the host launches the frontend before installing its unbundled driver libraries",
        )
        self.assertLess(install, verify)
        for package in ("libopengl0", "libegl1", "libgbm1", "libdrm2"):
            self.assertIn(package, workflow[install:verify])

    def test_runtime_loader_failure_retains_its_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / NAME
            app.touch()
            failure = subprocess.CalledProcessError(
                127, [str(app), "--version"], stderr="libOpenGL.so.0 is missing"
            )
            with patch(
                "sys.argv",
                [
                    "asset_metadata.py",
                    "verify",
                    str(app),
                    "--repository",
                    "obs-rust/obs-studio",
                ],
            ):
                with patch.object(metadata.subprocess, "run", side_effect=failure):
                    with self.assertRaisesRegex(
                        RuntimeError, "libOpenGL.so.0 is missing"
                    ):
                        metadata.main()

    def test_continuous_prune_preserves_current_pair_and_unrelated_assets(self):
        workflow = (ROOT / ".github/workflows/release-rust.yaml").read_text()
        start = workflow.index("            keep=")
        end = workflow.index("            gh api -X PATCH", start)
        prune = textwrap.dedent(workflow[start:end])
        legacy = "OBS_Studio-33.0.0-old-x86_64.AppImage"
        previous = "OBS-Studio-Rust-0.1.0-beta.20261013-linux-x86_64.AppImage"
        old = {legacy, legacy + ".zsync", previous, previous + ".zsync"}
        current = {NAME, NAME + ".zsync"}
        assets = sorted(old | current | {"SHA256SUMS", "release-notes.txt"})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in current:
                (root / name).touch()
            # Exercise the workflow's actual selector; only GitHub I/O is replaced.
            script = (
                """set -euo pipefail
gh() {
  case "$1 $2" in
    "release view") cat assets.txt ;;
    "release delete-asset") printf '%s\\n' "$4" ;;
    *) return 2 ;;
  esac
}
"""
                + prune
            )
            (root / "assets.txt").write_text("\n".join(assets) + "\n")
            result = subprocess.run(
                ["bash", "-c", script],
                cwd=root,
                env={**os.environ, "GITHUB_REPOSITORY": "obs-rust/obs-studio"},
                check=True,
                capture_output=True,
                text=True,
            )
        deleted = set(result.stdout.splitlines())
        self.assertFalse(
            current & deleted, "the newly uploaded canonical pair must survive"
        )
        self.assertEqual(
            deleted, old, "only superseded AppImage/zsync assets may be pruned"
        )

    def test_names_come_from_actual_executable_identity(self):
        self.assertEqual(metadata.asset_name(VERSION), NAME)
        self.assertEqual(
            metadata.asset_name("OBS Studio - 33.0.0"),
            "OBS-Studio-33.0.0-linux-x86_64.AppImage",
        )
        for value in (
            "",
            "OBS-Studio-Rust ../bad (based on OBS 33.0.0)",
            "unrelated output",
        ):
            with self.subTest(value=value), self.assertRaises(ValueError):
                metadata.asset_name(value)

    def test_updater_accepts_matching_assets_and_rejects_each_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / NAME
            app.write_bytes(b"actual payload for metadata checksum test")
            fields = {
                "Filename": NAME,
                "URL": NAME,
                "Length": str(app.stat().st_size),
                "SHA-1": hashlib.sha1(app.read_bytes()).hexdigest(),
            }
            sidecar = Path(str(app) + ".zsync")

            def write(values):
                sidecar.write_bytes(
                    (
                        "zsync: 0.6.2\n"
                        + "\n".join(f"{key}: {value}" for key, value in values.items())
                        + "\n\n"
                    ).encode()
                    + b"\xff\x00\x80"
                )

            write(fields)
            metadata.check_metadata(
                app, VERSION, TARGET, "obs-rust/obs-studio", require_update=True
            )
            for key, value in (
                ("Filename", "old.AppImage"),
                ("URL", "old.AppImage"),
                ("URL", "https://wrong.example/" + NAME),
                ("Length", "0"),
                ("SHA-1", "0" * 40),
            ):
                with self.subTest(key=key):
                    write({**fields, key: value})
                    with self.assertRaises(ValueError):
                        metadata.check_metadata(
                            app,
                            VERSION,
                            TARGET,
                            "obs-rust/obs-studio",
                            require_update=True,
                        )
            write(fields)
            for target in (
                "",
                TARGET.replace("OBS-Studio-Rust-*", "OBS_Studio-*"),
                TARGET.replace("obs-rust", "other"),
            ):
                with self.subTest(target=target), self.assertRaises(ValueError):
                    metadata.check_metadata(
                        app, VERSION, target, "obs-rust/obs-studio", require_update=True
                    )
            metadata.check_metadata(app, VERSION, "", "obs-rust/obs-studio")
            with self.assertRaises(ValueError):
                metadata.check_metadata(
                    app,
                    VERSION.replace("20261014", "20261015"),
                    "",
                    "obs-rust/obs-studio",
                )

    def test_workflows_preserve_the_canonical_name(self):
        # The build scripts moved into build.py (#899); the workflow calls it.
        appimage = (ROOT / ".github/workflows/appimage.yaml").read_text()
        appimage += (ROOT / "build-aux/appimage/build.py").read_text()
        release = (ROOT / ".github/workflows/release-rust.yaml").read_text()
        self.assertIn("asset_metadata.py name", appimage)
        self.assertIn(
            "continuous|OBS-Studio-Rust-*-linux-x86_64.AppImage.zsync", appimage
        )
        self.assertNotIn("OBS_Studio-", appimage)
        self.assertIn(
            "cp appimage/OBS-Studio-Rust-*-linux-x86_64.AppImage* dist/", release
        )
        self.assertNotIn("OBS_Studio-", release.split("            keep=", 1)[0])
        smoke = (ROOT / "build-aux/appimage/test-appimage.sh").read_text()
        self.assertIn("asset_metadata.py verify", appimage)
        self.assertIn("EXPECT_APPIMAGE_UPDATE", smoke)


if __name__ == "__main__":
    unittest.main()
