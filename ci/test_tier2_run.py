#!/usr/bin/env -S uv run --script
"""Tests for rust/tools/linux-validate/run.sh (obs-rust/obs-studio#135).

    uv run python -m unittest ci/test_tier2_run.py

run.sh is executed from two throwaway checkouts with stub `docker` and `git`
commands on PATH that record their arguments, so no container is started.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "rust" / "tools" / "linux-validate"

STUB = """#!/bin/sh
printf '%s\\n' "$*" >> "$STUB_LOG"
exit 0
"""


class Tier2VolumeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tier2-run-"))
        self.bin = self.tmp / "bin"
        self.bin.mkdir()
        for name in ("docker", "git"):
            stub = self.bin / name
            stub.write_text(STUB)
            stub.chmod(0o755)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def checkout(self, name: str) -> Path:
        """A fake checkout holding a copy of the linux-validate tools."""
        repo = self.tmp / name
        dest = repo / "rust" / "tools" / "linux-validate"
        shutil.copytree(TOOLS, dest)
        shutil.copy(ROOT / "rust-toolchain.toml", repo / "rust-toolchain.toml")
        for sub in ("plugins/obs-browser", "plugins/obs-websocket"):
            (repo / sub).mkdir(parents=True)
            (repo / sub / "placeholder").write_text("")
        return repo

    def docker_run_mounts(self, repo: Path, extra_env=None) -> dict[str, str]:
        self.calls = getattr(self, "calls", 0) + 1
        log = self.tmp / f"{repo.name}-{self.calls}.log"
        self.last_log = log
        env = {k: v for k, v in os.environ.items() if not k.startswith(("OBS_TIER2", "OBS_CCACHE"))}
        env.update(PATH=f"{self.bin}:{env['PATH']}", STUB_LOG=str(log), **(extra_env or {}))
        subprocess.run(["bash", str(repo / "rust/tools/linux-validate/run.sh")], env=env, check=True)
        runs = [line.split() for line in log.read_text().splitlines() if line.startswith("run ")]
        self.assertEqual(len(runs), 1, log.read_text())
        args = runs[0]
        mounts = {}
        for flag, value in zip(args, args[1:]):
            if flag == "-v":
                source, target = value.split(":")[:2]
                mounts[target] = source
        return mounts

    def test_build_volume_is_per_checkout(self):
        a = self.docker_run_mounts(self.checkout("a"))
        b = self.docker_run_mounts(self.checkout("b"))
        self.assertNotEqual(a["/build"], b["/build"])
        self.assertNotEqual(a["/build"], "obs-rust-linux-validate-build")

    def test_build_volume_is_stable(self):
        repo = self.checkout("a")
        self.assertEqual(self.docker_run_mounts(repo)["/build"], self.docker_run_mounts(repo)["/build"])

    def test_ccache_is_shared(self):
        a = self.docker_run_mounts(self.checkout("a"))
        b = self.docker_run_mounts(self.checkout("b"))
        self.assertIn("/ccache", a)
        self.assertEqual(a["/ccache"], b["/ccache"])

    def test_volume_override(self):
        mounts = self.docker_run_mounts(self.checkout("a"), {"OBS_TIER2_VOLUME": "custom-vol"})
        self.assertEqual(mounts["/build"], "custom-vol")

    def test_image_installs_pinned_toolchain(self):
        # rustup no longer installs a rust-toolchain.toml channel on demand, so
        # the image must bake in exactly the pinned one.
        repo = self.checkout("a")
        (repo / "rust-toolchain.toml").write_text('[toolchain]\nchannel = "1.2.3"\n')
        self.docker_run_mounts(repo)
        builds = [line.split() for line in self.last_log.read_text().splitlines() if line.startswith("build ")]
        self.assertEqual(len(builds), 1, self.last_log.read_text())
        self.assertIn("RUST_TOOLCHAIN=1.2.3", builds[0])


class StaleBuildTest(unittest.TestCase):
    """validate.sh starts a /build dir over when the Rust toolchain changed.

    The /build volume outlives image rebuilds, and Corrosion caches the old
    toolchain's rustc and cargo paths, so after a toolchain pin change the
    old configure fails with "Could not find toolchain".
    """

    HELPER = TOOLS / "stale-build.sh"

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="stale-build-"))
        self.build = self.tmp / "rust-ON"

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def reset(self, rustc_version: str) -> None:
        subprocess.run(
            ["bash", "-c", 'source "$1"; reset_stale_build "$2" "$3"', "-", str(self.HELPER), str(self.build), rustc_version],
            check=True,
        )

    def configured(self, stamp: str | None) -> None:
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("Rust_TOOLCHAIN:STRING=stable\n")
        if stamp is not None:
            (self.tmp / "rust-ON.rustc").write_text(stamp + "\n")

    def test_same_toolchain_keeps_build(self):
        self.configured("rustc 1.98.1")
        self.reset("rustc 1.98.1")
        self.assertTrue((self.build / "CMakeCache.txt").exists())

    def test_changed_toolchain_starts_over(self):
        self.configured("rustc 1.99.0")
        self.reset("rustc 1.98.1")
        self.assertFalse((self.build / "CMakeCache.txt").exists())
        self.assertEqual((self.tmp / "rust-ON.rustc").read_text().strip(), "rustc 1.98.1")

    def test_unstamped_build_starts_over(self):
        # Volumes configured before the stamp existed.
        self.configured(None)
        self.reset("rustc 1.98.1")
        self.assertFalse((self.build / "CMakeCache.txt").exists())
        self.assertEqual((self.tmp / "rust-ON.rustc").read_text().strip(), "rustc 1.98.1")

    def test_fresh_build_is_stamped(self):
        self.reset("rustc 1.98.1")
        self.assertEqual((self.tmp / "rust-ON.rustc").read_text().strip(), "rustc 1.98.1")


if __name__ == "__main__":
    unittest.main()
