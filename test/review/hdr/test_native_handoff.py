#!/usr/bin/env -S uv run --script
"""Tests run in disposable repositories and never touch the caller's Git index."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from prepare_native import MARKER, blob, commit_snapshot, git, verify
from fix_macos_test_runtime import candidates, rpaths


class CommitTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(["git", "init", "-q", "-b", "owner"], cwd=self.root, check=True)
        git(self.root, "config", "user.name", "Fixture")
        git(self.root, "config", "user.email", "fixture@example.invalid")
        git(self.root, "config", "commit.gpgSign", "false")
        for name in ("native.c", "unrelated.txt"):
            (self.root / name).write_text("original\n")
        git(self.root, "add", "native.c", "unrelated.txt")
        git(self.root, "commit", "-qm", "fixture")
        self.parent = git(self.root, "rev-parse", "HEAD")
        git(self.root, "checkout", "--detach", "-q", self.parent)
        (self.root / "native.c").write_text("integrated\n")
        self.expected = {"native.c": blob(b"integrated\n")}

    def test_detached_commit_preserves_owner_branch(self):
        head = commit_snapshot(self.root, self.parent, self.expected)
        self.assertNotEqual(head, self.parent)
        self.assertEqual(git(self.root, "rev-parse", "refs/heads/owner"), self.parent)
        self.assertEqual(git(self.root, "status", "--porcelain"), "")
        self.assertEqual(verify(self.root, {"native.c"})["files"], self.expected)
        self.assertEqual(set(git(self.root, "diff-tree", "--no-commit-id", "--name-only", "-r", head).splitlines()),
                         {"native.c", MARKER})

    def test_attached_refusal(self):
        git(self.root, "checkout", "-q", "owner")
        with self.assertRaises(RuntimeError): commit_snapshot(self.root, self.parent, self.expected)
        self.assertFalse((self.root / MARKER).exists())

    def test_unrelated_staged_refusal(self):
        (self.root / "unrelated.txt").write_text("changed\n")
        git(self.root, "add", "unrelated.txt")
        with self.assertRaises(RuntimeError): commit_snapshot(self.root, self.parent, self.expected)
        self.assertEqual(git(self.root, "rev-parse", "HEAD"), self.parent)

    def test_unrelated_unstaged_refusal(self):
        (self.root / "unrelated.txt").write_text("changed\n")
        with self.assertRaises(RuntimeError): commit_snapshot(self.root, self.parent, self.expected)

    def test_hash_mismatch_refusal(self):
        with self.assertRaises(RuntimeError): commit_snapshot(self.root, self.parent, {"native.c": "0" * 40})
        self.assertFalse((self.root / MARKER).exists())

    def test_manifest_detects_source_drift(self):
        commit_snapshot(self.root, self.parent, self.expected)
        (self.root / "native.c").write_text("changed again\n")
        with self.assertRaises(RuntimeError): verify(self.root, {"native.c"})


class RuntimeTests(unittest.TestCase):
    def test_only_registered_build_tests(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); build = root / "build"; build.mkdir()
            test = build / "test_example"; test.touch()
            app = build / "obs-studio"; app.touch()
            outside = root / "test_outside"; outside.touch()
            library = build / "test_aux.dylib"; library.touch()
            spec = {"tests": [{"command": [str(p)]} for p in (test, app, outside, library, test)]}
            self.assertEqual(candidates(spec, build), [test])

    def test_missing_registered_binary(self):
        with tempfile.TemporaryDirectory() as temp:
            build = Path(temp)
            with self.assertRaises(RuntimeError):
                candidates({"tests": [{"command": [str(build / "test_missing")]}]}, build)

    def test_rpath_with_spaces(self):
        output = "Load command 1\n          cmd LC_RPATH\n      cmdsize 64\n         path /tmp/With Spaces/lib (offset 12)\n"
        self.assertEqual(rpaths(output), {"/tmp/With Spaces/lib"})


if __name__ == "__main__":
    unittest.main()
