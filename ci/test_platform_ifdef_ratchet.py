#!/usr/bin/env -S uv run --script
"""Platform conditionals in shared C/C++ only go down (#847, #848).

    uv run python -m unittest ci/test_platform_ifdef_ratchet.py
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import platform_ifdef_ratchet as r  # noqa: E402


class CountTest(unittest.TestCase):
    def lines(self, text: str) -> list[int]:
        return [n for n, _ in r.count_platform_conditionals(text)]

    def test_counts_every_conditional_form(self):
        text = "\n".join(
            [
                "#ifdef _WIN32",  # 1
                "#endif",
                "#ifndef __APPLE__",  # 3
                "#endif",
                "#if defined(__linux__) || defined(__FreeBSD__)",  # 5
                "#elif TARGET_OS_MAC",  # 6
                "#else",
                "#endif",
                "  #  if _MSC_VER >= 1900",  # 9
                "#endif",
                "#if defined(OS_WINDOWS) && !defined(OS_MACOS)",  # 11
                "#endif",
                "#ifdef __unix__",  # 13
                "#ifdef __OpenBSD__",  # 14
                "#if OS_LINUX || OS_FREEBSD",  # 15
                "#if defined(_WIN64) || defined(__MACH__)",  # 16
            ]
        )
        self.assertEqual(self.lines(text), [1, 3, 5, 6, 9, 11, 13, 14, 15, 16])

    def test_bare_defined_and_negated_forms(self):
        text = "\n".join(
            [
                "#if _WIN32",  # 1
                "#elif __linux__",  # 2
                "#elif WIN32",  # 3
                "#ifdef WIN32",  # 4
                "#ifndef WIN32",  # 5
                "#if !defined(_WIN32)",  # 6
                "#if !defined WIN32 && !defined __APPLE__",  # 7
                "#if defined( __linux__ )",  # 8
                "#if (defined(_MSC_VER))",  # 9
                "#if !_WIN32",  # 10
                "#elif defined WIN32",  # 11
                "#if defined(WIN32) || defined(_WIN32)",  # 12
            ]
        )
        self.assertEqual(self.lines(text), list(range(1, 13)))

    def test_ignores_other_lines(self):
        text = "\n".join(
            [
                "#ifdef ENABLE_HEVC",
                "#if defined(DEBUG)",
                "#define FOO _WIN32",
                "#else",
                "#endif /* _WIN32 */",
                "// #ifdef _WIN32",
                "int _WIN32_x = 0;",
                "#if MY_OS_WINDOWS_FLAG",
                "#ifdef WIN32_LEAN",
                "#ifdef MYWIN32",
                "#include <windows.h>",
            ]
        )
        self.assertEqual(self.lines(text), [])

    def test_reports_the_line_text(self):
        self.assertEqual(r.count_platform_conditionals("a\n#ifdef _WIN32\n"), [(2, "#ifdef _WIN32")])


class PathTest(unittest.TestCase):
    def test_platform_named_paths(self):
        for path in [
            "libobs/obs-nix.c",
            "libobs/obs-windows.c",
            "libobs/obs-cocoa.m",
            "libobs/util/platform-nix.c",
            "libobs/util/threading-windows.c",
            "frontend/utility/platform-x11.cpp",
            "frontend/utility/platform-osx.mm",
            "frontend/utility/platform-windows.cpp",
            "frontend/utility/platform-freebsd.cpp",
            "plugins/linux-v4l2/v4l2-input.c",
            "plugins/win-capture/dc-capture.c",
            "plugins/mac-avcapture/OBSAvCapture.m",
            "plugins/linux-pipewire/pipewire.c",
            "plugins/linux-pulseaudio/pulse-input.c",
            "plugins/linux-alsa/alsa-input.c",
            "libobs/util/pipe-posix.c",
            "libobs/audio-monitoring/osx/coreaudio-output.c",
            "libobs/audio-monitoring/pulse/pulseaudio-output.c",
            "libobs/graphics/Foo.mm",
            "libobs-opengl/gl-wayland-egl.c",
            "libobs-opengl/gl-x11-glx.c",
        ]:
            with self.subTest(path=path):
                self.assertTrue(r.is_platform_path(path))

    def test_shared_paths(self):
        for path in [
            "libobs/obs.c",
            "libobs/obs-video.c",
            "frontend/OBSApp.cpp",
            "frontend/utility/platform.hpp",
            "plugins/obs-ffmpeg/obs-ffmpeg-output.c",
            "plugins/obs-filters/macro-helpers.c",
            "shared/bpm/bpm.c",
            "libobs/util/platform.c",
        ]:
            with self.subTest(path=path):
                self.assertFalse(r.is_platform_path(path))

    def test_scope(self):
        for path in ["libobs/obs.c", "frontend/OBSApp.cpp", "shared/a/b.hpp", "plugins/obs-x264/obs-x264.c", "libobs/a.h", "frontend/x.mm"]:
            with self.subTest(path=path):
                self.assertTrue(r.in_scope(path))
        for path in [
            "deps/jansson/src/x.c",
            "plugins/obs-websocket/x.cpp",
            "plugins/obs-browser/x.cpp",
            "plugins/obs-outputs/librtmp/rtmp.c",
            "plugins/obs-filters/rnnoise/src/arch.h",
            "cmake/x.c",
            "test/cmocka/x.c",
            "rust/obs-c-oracle/legacy/x.c",
            "libobs/CMakeLists.txt",
            "frontend/readme.md",
            "frontend/x.rs",
            "UI/x.cpp",
        ]:
            with self.subTest(path=path):
                self.assertFalse(r.in_scope(path))


class CompareTest(unittest.TestCase):
    def test_equal_is_clean(self):
        self.assertEqual(r.compare({"a.c": 2}, {"a.c": 2}), ([], []))

    def test_increase_fails(self):
        bad, _ = r.compare({"a.c": 3}, {"a.c": 2})
        self.assertEqual(len(bad), 1)
        self.assertIn("a.c", bad[0])

    def test_new_file_fails(self):
        bad, _ = r.compare({"b.c": 1}, {})
        self.assertEqual(len(bad), 1)

    def test_decrease_is_a_hint_only(self):
        bad, hints = r.compare({"a.c": 1}, {"a.c": 2})
        self.assertEqual(bad, [])
        self.assertEqual(len(hints), 1)

    def test_vanished_file_is_a_hint(self):
        bad, hints = r.compare({}, {"a.c": 2})
        self.assertEqual(bad, [])
        self.assertEqual(len(hints), 1)


class MainTest(unittest.TestCase):
    """End to end over a throwaway git-free tree via --root/--files."""

    def setUp(self):
        import tempfile

        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        (self.root / "libobs").mkdir()
        (self.root / "libobs" / "shared.c").write_text("#ifdef _WIN32\nint a;\n#endif\n#ifdef __APPLE__\n#endif\n")
        (self.root / "libobs" / "obs-nix.c").write_text("#ifdef __linux__\n#endif\n")
        self.baseline = self.root / "baseline.json"

    def run_main(self, *extra: str) -> int:
        import contextlib
        import io

        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = r.main(["--root", str(self.root), "--baseline", str(self.baseline), "--files", "libobs/shared.c", "libobs/obs-nix.c", *extra])
        self.output = out.getvalue()
        return code

    def test_update_then_pass_then_regress(self):
        self.assertEqual(self.run_main("--update"), 0)
        self.assertEqual(json.loads(self.baseline.read_text()), {"libobs/shared.c": 2})
        self.assertEqual(self.run_main(), 0)
        (self.root / "libobs" / "shared.c").write_text("#ifdef _WIN32\n#endif\n#ifdef __APPLE__\n#endif\n#if _MSC_VER\n#endif\n")
        self.assertEqual(self.run_main(), 1)
        self.assertIn("libobs/shared.c:5", self.output)
        self.assertIn("platform facade", self.output)

    def test_decrease_passes_with_hint(self):
        self.baseline.write_text(json.dumps({"libobs/shared.c": 5}))
        self.assertEqual(self.run_main(), 0)
        self.assertIn("--update", self.output)


class RepoBaselineTest(unittest.TestCase):
    def test_committed_baseline_matches_the_tree(self):
        """The committed baseline is the tree's real counts (run by ./lint)."""
        self.assertEqual(r.main([]), 0)


if __name__ == "__main__":
    unittest.main()
