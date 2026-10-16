#!/usr/bin/env -S uv run --script
"""Which CI jobs a change needs (#436): Linux always; core code adds the
Rust-libobs OBS build; macOS or Windows code adds that platform's jobs.
Platform is decided by path alone (#897): platform code lives in
platform-named files and directories, and core files carry no platform
defines (ci/platform_ifdef_ratchet.py).

    uv run python -m unittest ci/test_ci_scope.py
"""

from __future__ import annotations

import unittest

from ci_scope import scope
from platform_paths import LINUX, MACOS, WINDOWS, platforms

CPP_TESTS = {"test/cmocka/test_source_select_list.cpp"}


class CiScopeTest(unittest.TestCase):
    def check(self, files, *, core=False, macos=False, windows=False):
        got = scope(files, lambda path: path in CPP_TESTS)
        self.assertEqual((got.core, got.macos, got.windows), (core, macos, windows), files)

    def test_docs_and_linux_only_need_linux(self):
        self.check(["docs/rust-port/naming.md"])
        self.check(["plugins/linux-pipewire/pipewire.c"])
        self.check(["libobs/obs-nix-wayland.c"], core=True)
        self.check(["libobs/obs-nix.c"], core=True)
        self.check(["frontend/widgets/OBSBasicWindow.cpp"])  # "Window", not Windows

    def test_core_code(self):
        self.check(["libobs/obs-source.c"], core=True)
        self.check(["rust/obs-util/src/dstr.rs"], core=True)
        self.check(["Cargo.lock"], core=True)

    def test_core_file_content_is_not_read(self):
        # Core files carry no platform defines (the ratchet lint enforces
        # it), so a core file is Linux-only whatever it contains (#897).
        self.check(["libobs/obs.c"], core=True)
        self.check(["frontend/OBSApp.cpp"])
        self.check(["plugins/obs-ffmpeg/CMakeLists.txt"])

    def test_macos_code(self):
        self.check(["plugins/mac-capture/mac-display-capture.m"], macos=True)
        self.check(["frontend/utility/CrashHandler_MacOS.mm"], macos=True)
        self.check(["libobs/util/platform-cocoa.m"], core=True, macos=True)
        self.check(["libobs/util/apple/cfstring-utils.h"], core=True, macos=True)
        self.check(["cmake/macos/compilerconfig.cmake"], macos=True)
        self.check(["plugins/coreaudio-encoder/encoder.cpp"], macos=True)
        self.check(["libobs/audio-monitoring/osx/coreaudio-output.c"], core=True, macos=True)

    def test_posix_code_reaches_macos(self):
        # macOS builds the POSIX sources and util/platform-nix.c too
        # (libobs/cmake/os-macos.cmake).
        self.check(["libobs/util/threading-posix.c"], core=True, macos=True)
        self.check(["libobs/util/pipe-posix.c"], core=True, macos=True)
        self.check(["libobs/util/platform-nix.c"], core=True, macos=True)
        self.check(["shared/ipc-util/ipc-util/pipe-posix.c"], macos=True)
        self.check(["plugins/text-freetype2/find-font-unix.c"], macos=True)
        self.check(["rust/obs-util/src/platform/unix/pipe.rs"], core=True, macos=True)
        # The rest of the nix family is Linux and BSD only.
        self.check(["libobs/util/platform-nix-dbus.c"], core=True)

    def test_windows_code(self):
        self.check(["plugins/win-capture/graphics-hook/d3d11-capture.cpp"], windows=True)
        self.check(["frontend/utility/CrashHandler_Windows.cpp"], windows=True)
        self.check(["libobs/util/threading-windows.h"], core=True, windows=True)
        self.check(["libobs/obs-win-crash-handler.c"], core=True, windows=True)
        self.check(["deps/w32-pthreads/pthread.c"], windows=True)
        self.check(["cmake/windows/compilerconfig.cmake"], windows=True)
        self.check(["rust/obs-util/src/platform/windows/pipe.rs"], core=True, windows=True)

    def test_rust_platform_directories_map_exactly(self):
        self.check(["rust/obs-nvenc/src/platform/macos.rs"], core=True, macos=True)
        self.check(["rust/obs-nvenc/src/platform/windows.rs"], core=True, windows=True)
        self.check(["rust/obs-media-io/src/platform/unix.rs"], core=True, macos=True)
        self.check(["rust/obs-util/src/platform/linux/x.rs"], core=True)

    def test_shared_build_and_ci_changes_need_every_platform(self):
        for path in [
            "CMakeLists.txt",
            "cmake/common/helpers_common.cmake",
            ".github/workflows/build-project.yaml",
            "buildspec.json",
            "libobs/cmake/rust-exports.map",
            "rust/obs-util/src/platform/mod.rs",
        ]:
            got = scope([path])
            self.assertTrue(got.macos and got.windows, path)

    def test_cpp_cmocka_tests_and_test_build_config_need_every_platform(self):
        # Each platform's Tier 2 compiles them with its own compiler (#610).
        for path in [
            "test/cmocka/test_source_select_list.cpp",
            "test/cmocka/tests.d/test_source_select_list.cmake",
            "test/cmocka/CMakeLists.txt",
        ]:
            got = scope([path], lambda p: p in CPP_TESTS)
            self.assertTrue(got.macos and got.windows, path)

    def test_c_cmocka_tests_stay_linux_only(self):
        self.check(["test/cmocka/test_pipe_wait_error.c"])
        self.check(["test/cmocka/tests.d/test_pipe_wait_error.cmake"])


class PlatformPathsTest(unittest.TestCase):
    def test_classification(self):
        self.assertIsNone(platforms("libobs/obs.c"))
        self.assertIsNone(platforms("frontend/utility/platform.hpp"))  # the facade
        self.assertEqual(platforms("frontend/utility/platform-x11.cpp"), {LINUX})
        self.assertEqual(platforms("frontend/utility/platform-osx.mm"), {MACOS})
        self.assertEqual(platforms("frontend/utility/platform-windows.cpp"), {WINDOWS})
        self.assertEqual(platforms("libobs/util/threading-posix.c"), {LINUX, MACOS})
        self.assertEqual(platforms("libobs/util/platform-nix.c"), {LINUX, MACOS})
        self.assertEqual(platforms("plugins/linux-v4l2/v4l2-input.c"), {LINUX})


if __name__ == "__main__":
    unittest.main()
