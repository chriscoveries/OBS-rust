#!/usr/bin/env -S uv run --script
"""Platform conditionals in shared C/C++ only go down (#847, #848), and a
platform file names only its own OS (#1051).

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
            "libobs/obs-nix.c.h",
            "libobs/obs-windows.c.h",
            "libobs/obs-cocoa.m.h",
            "libobs/util/platform-nix.c.h",
            "libobs/util/threading-windows.c.h",
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
            "libobs/util/pipe-posix.c.h",
            "libobs/audio-monitoring/osx/coreaudio-output.c.h",
            "libobs/audio-monitoring/pulse/pulseaudio-output.c.h",
            "libobs/graphics/Foo.mm",
            "libobs-opengl/gl-wayland-egl.c.h",
            "libobs-opengl/gl-x11-glx.c",
            # Same classifier as ci/ci_scope.py (ci/platform_paths.py, #1051).
            "plugins/obs-ffmpeg/vaapi-utils.c",
            "plugins/sndio/sndio-input.c",
        ]:
            with self.subTest(path=path):
                self.assertTrue(r.is_platform_path(path))

    def test_shared_paths(self):
        for path in [
            "libobs/obs.c.h",
            "libobs/obs-video.c.h",
            "frontend/OBSApp.cpp",
            "frontend/utility/platform.hpp",
            "plugins/obs-ffmpeg/obs-ffmpeg-output.c",
            "plugins/obs-filters/macro-helpers.c",
            "shared/bpm/bpm.c.h",
            "libobs/util/platform.c.h",
            # platform- names a shared facade, not an OS (#1051).
            "libobs/util/platform-number.c.h",
            "shared/ipc-util/ipc-util/platform-pipe.h",
        ]:
            with self.subTest(path=path):
                self.assertFalse(r.is_platform_path(path))

    def test_scope(self):
        for path in ["libobs/obs.c.h", "frontend/OBSApp.cpp", "shared/a/b.hpp", "plugins/obs-x264/obs-x264.c", "libobs/a.h", "frontend/x.mm"]:
            with self.subTest(path=path):
                self.assertTrue(r.in_scope(path))
        for path in [
            "deps/jansson/src/x.c",
            "plugins/obs-websocket/x.cpp",
            "plugins/obs-browser/x.cpp",
            "plugins/obs-outputs/librtmp/rtmp.c",
            "plugins/obs-filters/rnnoise/src/arch.h",
            "plugins/decklink/mac/decklink-sdk/DeckLinkAPI.h",
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


class StripTest(unittest.TestCase):
    """Comments and strings never hide or invent a conditional (#1051)."""

    def lines(self, text: str) -> list[int]:
        return [n for n, _ in r.count_platform_conditionals(text)]

    def test_macro_in_a_trailing_comment_is_not_counted(self):
        self.assertEqual(self.lines("#ifdef FOO /* _WIN32 */\n#endif\n#if BAR // __APPLE__\n"), [])

    def test_directive_inside_a_block_comment_is_not_counted(self):
        self.assertEqual(self.lines("/*\n#ifdef _WIN32\n#endif\n*/\n#ifdef __APPLE__\n"), [5])

    def test_comment_opener_in_a_string_hides_nothing(self):
        self.assertEqual(self.lines('const char *s = "a/*b";\n#ifdef _WIN32\n#endif\n'), [2])
        self.assertEqual(self.lines("char c = '\"';\n#ifdef _WIN32\n"), [2])

    def test_raw_string_body_is_not_code(self):
        self.assertEqual(self.lines('auto s = R"x(\n#ifdef _WIN32\n)x";\n#ifdef __APPLE__\n'), [4])

    def test_continued_conditional_counts_at_its_first_line(self):
        self.assertEqual(self.lines("int a;\n#if defined(FOO) || \\\n    defined(_WIN32)\n#endif\n"), [2])

    def test_crlf_line_endings(self):
        self.assertEqual(self.lines("int a;\r\n#if FOO || \\\r\n  _WIN32\r\n#endif\r\n"), [2])

    def test_compiler_and_arch_macros_are_not_host_macros(self):
        self.assertEqual(self.lines("#ifdef __GNUC__\n#if defined(__x86_64__)\n#if __clang__\n#ifdef NDEBUG\n"), [])


class FindingsTest(unittest.TestCase):
    """A host file may only name its own OS; shared files name none (#1051)."""

    def lines(self, path: str, text: str) -> list[int]:
        return [f[0] for f in r.findings(path, text)]

    def test_shared_file_conditionals_count(self):
        self.assertEqual(self.lines("libobs/obs.c.h", "#ifdef __APPLE__\n#endif\n"), [1])

    def test_own_os_macro_in_a_host_file_is_fine(self):
        self.assertEqual(self.lines("libobs/obs-cocoa.m.h", "#ifdef __APPLE__\n#endif\n"), [])
        self.assertEqual(self.lines("plugins/win-capture/a.c", "#if defined(_WIN64)\n#endif\n"), [])
        self.assertEqual(self.lines("libobs/obs-nix.c.h", "#if defined(__linux__) || defined(__FreeBSD__)\n"), [])

    def test_other_os_macro_in_a_host_file_fails(self):
        self.assertEqual(self.lines("libobs/obs-cocoa.m.h", "#ifdef _WIN32\n#endif\n"), [1])
        self.assertEqual(self.lines("libobs/x.m", "int a;\n#ifndef _MSC_VER\n"), [2])
        self.assertEqual(self.lines("libobs/obs-nix.c.h", "#ifdef __APPLE__\n"), [1])
        self.assertEqual(self.lines("libobs/obs-windows.c.h", "#ifdef __linux__\n"), [1])

    def test_posix_file_may_split_its_two_oses_one_at_a_time(self):
        self.assertEqual(self.lines("libobs/util/threading-posix.c.h", "#ifdef __APPLE__\n#elif defined(__linux__)\n"), [])
        self.assertEqual(self.lines("libobs/util/threading-posix.c.h", "#ifdef __unix__\n"), [])
        self.assertEqual(self.lines("libobs/obs-nix.c.h", "#ifdef __unix__\n"), [])

    def test_conditional_naming_two_oses_in_a_host_file_fails(self):
        self.assertEqual(self.lines("libobs/obs-nix.c.h", "#if defined(_WIN32) || defined(__APPLE__)\n"), [1])
        self.assertEqual(self.lines("libobs/util/threading-posix.c.h", "#if defined(__APPLE__) || defined(__linux__)\n"), [1])

    def test_os_header_in_a_shared_file_counts(self):
        self.assertEqual(self.lines("libobs/obs.c.h", "#include <windows.h>\n"), [1])
        self.assertEqual(self.lines("libobs/obs.c.h", "#include <Windows.h>\n"), [1])
        self.assertEqual(self.lines("libobs/obs.c.h", "#include <d3d11.h>\n#include <dxgi1_2.h>\n"), [1, 2])
        self.assertEqual(self.lines("frontend/a.mm", "#import <AppKit/AppKit.h>\n"), [])
        self.assertEqual(self.lines("frontend/a.cpp", "#import <AppKit/AppKit.h>\n"), [1])
        self.assertEqual(self.lines("libobs/obs.c.h", "#include <X11/Xlib.h>\n#include <wayland-client.h>\n"), [1, 2])
        self.assertEqual(self.lines("libobs/obs.c.h", "#include <unistd.h>\n#include <sys/mman.h>\n"), [1, 2])

    def test_portable_or_quoted_headers_are_not_os_headers(self):
        text = '#include <stdio.h>\n#include <pthread.h>\n#include <fcntl.h>\n#include "linux/decklink.h"\n#include "util/platform.h"\n'
        self.assertEqual(self.lines("libobs/obs.c.h", text), [])

    def test_os_header_in_the_right_host_file_is_fine(self):
        self.assertEqual(self.lines("libobs/util/threading-posix.c.h", "#include <unistd.h>\n"), [])
        self.assertEqual(self.lines("libobs/obs-nix.c.h", "#include <unistd.h>\n#include <X11/Xlib.h>\n"), [])
        self.assertEqual(self.lines("libobs/obs-windows.c.h", "#include <windows.h>\n"), [])

    def test_os_header_in_the_wrong_host_file_fails(self):
        self.assertEqual(self.lines("libobs/obs-nix.c.h", "#include <windows.h>\n"), [1])
        self.assertEqual(self.lines("libobs/obs-windows.c.h", "#include <unistd.h>\n"), [1])
        self.assertEqual(self.lines("libobs/util/threading-posix.c.h", "#include <windows.h>\n"), [1])

    def test_finding_says_why(self):
        (_, text, why), = r.findings("libobs/obs-cocoa.m.h", "#ifdef _WIN32\n")
        self.assertEqual(text, "#ifdef _WIN32")
        self.assertIn("windows", why)
        self.assertIn("macos", why)


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
        self.exceptions = self.root / "exceptions.toml"

    def write_exceptions(self, body: str) -> None:
        self.exceptions.write_text(body)

    def run_main(self, *extra: str, files: list[str] | None = None) -> int:
        import contextlib
        import io

        files = files or ["libobs/shared.c", "libobs/obs-nix.c"]
        out = io.StringIO()
        args = ["--root", str(self.root), "--baseline", str(self.baseline), "--exceptions", str(self.exceptions)]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = r.main([*args, "--files", *files, *extra])
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

    def test_wrong_os_in_a_host_file_fails(self):
        self.assertEqual(self.run_main("--update"), 0)
        (self.root / "libobs" / "obs-nix.c").write_text("#ifdef __linux__\n#endif\n#ifdef _WIN32\n#endif\n")
        self.assertEqual(self.run_main(), 1)
        self.assertIn("libobs/obs-nix.c:3", self.output)

    def test_exception_skips_a_public_header(self):
        (self.root / "libobs" / "c99defs.h").write_text("#ifdef _MSC_VER\n#endif\n")
        self.write_exceptions('[[exception]]\npath = "libobs/c99defs.h"\nreason = "dllexport vs visibility"\n')
        self.assertEqual(self.run_main("--update", files=["libobs/c99defs.h", "libobs/shared.c"]), 0)
        self.assertEqual(json.loads(self.baseline.read_text()), {"libobs/shared.c": 2})

    def test_exception_must_be_a_header_with_a_reason_and_findings(self):
        (self.root / "libobs" / "clean.h").write_text("int a;\n")
        for body, needle in [
            ('[[exception]]\npath = "libobs/shared.c"\nreason = "x"\n', "header"),
            ('[[exception]]\npath = "libobs/shared.h"\n', "reason"),
            ('[[exception]]\npath = "libobs/missing.h"\nreason = "x"\n', "missing"),
            ('[[exception]]\npath = "libobs/clean.h"\nreason = "x"\n', "remove"),
        ]:
            with self.subTest(body=body):
                (self.root / "libobs" / "shared.h").write_text("#ifdef _WIN32\n#endif\n")
                self.write_exceptions(body)
                self.assertEqual(self.run_main(files=["libobs/shared.c", "libobs/clean.h", "libobs/shared.h"]), 1)
                self.assertIn(needle, self.output)

    def test_decrease_passes_with_hint(self):
        self.baseline.write_text(json.dumps({"libobs/shared.c": 5}))
        self.assertEqual(self.run_main(), 0)
        self.assertIn("--update", self.output)


# libobs core files whose host code moved behind libobs/obs-host.h (#1051).
FACADED = (
    "libobs/obs.c.h",
    "libobs/obs-display.c.h",
    "libobs/obs-internal.h",
    "libobs/obs-module.c.h",
    "libobs/obs-source.c.h",
    "libobs/obs-video.c.h",
    "libobs/obs-video-gpu-encode.c.h",
)


class FacadeTest(unittest.TestCase):
    def test_libobs_core_files_have_no_host_code(self):
        for path in FACADED:
            with self.subTest(path=path):
                text = (r.ROOT / path).read_text(encoding="utf-8")
                self.assertEqual(r.findings(path, text), [])

    def test_facade_and_noop_files_name_no_host(self):
        files = sorted(p for p in (r.ROOT / "libobs/obs-host").rglob("*") if p.name.endswith((".h", ".hpp")))
        self.assertGreater(len(files), 10)
        for file in files:
            path = file.relative_to(r.ROOT).as_posix()
            with self.subTest(path=path):
                self.assertIsNone(r.platforms(path))  # shared, not a host file
                self.assertEqual(r.findings(path, file.read_text(encoding="utf-8")), [])

    def test_every_capability_has_a_noop_and_every_host_builds_one_of_each(self):
        """obs-host/<cap>.h has obs-host/noop/<cap>.hpp, and each host file
        either includes that no-op header or defines the capability's
        functions itself (BUILD.md: no-ops are headers the host includes)."""
        import re

        caps = sorted(p.stem for p in (r.ROOT / "libobs/obs-host").glob("*.h") if p.stem != "defs")
        noops = sorted(p.stem for p in (r.ROOT / "libobs/obs-host/noop").glob("*.hpp"))
        self.assertEqual(caps, noops)
        host_file = {"WINDOWS": "obs-windows.c.h", "MACOS": "obs-cocoa.m.h", "LINUX": "obs-nix.c.h"}
        for os_name, source in host_file.items():
            impl = (r.ROOT / "libobs" / source).read_text()
            for cap in caps:
                header = (r.ROOT / f"libobs/obs-host/{cap}.h").read_text()
                funcs = re.findall(r"OBS_HOST_EXTERN [^;(]*?\b(obs_host_\w+)\(", header)
                self.assertTrue(funcs, cap)
                defined = [f for f in funcs if re.search(rf"^[^\s].*\b{f}\(", impl, re.M)]
                with self.subTest(os=os_name, capability=cap):
                    if f'#include "obs-host/noop/{cap}.hpp"' in impl:
                        self.assertEqual(defined, [], "both the no-op and the host file define it")
                    else:
                        self.assertEqual(defined, funcs, "neither the no-op nor the host file defines it")


class RepoBaselineTest(unittest.TestCase):
    def test_committed_baseline_matches_the_tree(self):
        """The committed baseline is the tree's real counts (run by ./lint)."""
        self.assertEqual(r.main([]), 0)


if __name__ == "__main__":
    unittest.main()
