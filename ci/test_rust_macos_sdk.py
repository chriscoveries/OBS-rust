#!/usr/bin/env -S uv run --script
"""The Rust import must not override the macOS SDK chosen by CMake (#777).

Run with: uv run python -m unittest ci/test_rust_macos_sdk.py

Configure the real rust.cmake with a stand-in for Corrosion 0.5.2's imported
target. No Rust build or Apple SDK is required: the regression is the explicit
CLT library search path propagated to libobs, ahead of the selected sysroot.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLT_LIB = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib"


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class RustMacosSdkTest(unittest.TestCase):
    def link_directories(self, macos: bool) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            # Model the imported target's interface, without fetching/building
            # Corrosion. Keep another path to catch overly broad removal.
            (source / "FetchContent.cmake").write_text(
                "function(FetchContent_Declare)\nendfunction()\n"
                "function(FetchContent_MakeAvailable)\nendfunction()\n"
                "function(corrosion_import_crate)\n"
                "  add_library(libobs_rust-static STATIC IMPORTED GLOBAL)\n"
                '  set_property(TARGET libobs_rust-static PROPERTY IMPORTED_LOCATION "/unused/libobs_rust.a")\n'
                "  set_property(TARGET libobs_rust-static PROPERTY INTERFACE_LINK_DIRECTORIES\n"
                f'    "{CLT_LIB};/other/native/libs")\n'
                "endfunction()\n"
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\n"
                "project(SdkRegression C)\n"
                'list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")\n'
                "add_library(libobs STATIC dummy.c)\n"
                # rust.cmake configures libobs's Rust twin (#1065).
                "add_library(libobs-with-rust STATIC dummy.c)\n"
                f'set(OS_MACOS {"ON" if macos else "OFF"})\n'
                f'include("{ROOT.as_posix()}/libobs/cmake/rust.cmake")\n'
                "get_target_property(paths libobs_rust-static INTERFACE_LINK_DIRECTORIES)\n"
                'file(WRITE "${CMAKE_BINARY_DIR}/paths.txt" "${paths}")\n'
            )
            (source / "dummy.c").write_text("void dummy(void) {}\n")
            build = source / "build"
            result = subprocess.run(
                ["cmake", "-S", str(source), "-B", str(build)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (build / "paths.txt").read_text().split(";")

    def test_macos_uses_selected_sdk_instead_of_clt_search_path(self):
        self.assertEqual(self.link_directories(True), ["/other/native/libs"])

    def test_other_platforms_keep_imported_search_paths(self):
        self.assertEqual(self.link_directories(False), [CLT_LIB, "/other/native/libs"])


if __name__ == "__main__":
    unittest.main()
