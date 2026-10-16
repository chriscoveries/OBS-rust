"""Which platforms a path belongs to (obs-rust/obs-studio#897).

One answer for both users: ci/ci_scope.py starts a platform's CI jobs when a
file of that platform changes, and ci/platform_ifdef_ratchet.py allows
platform conditionals only in such files. Everything else is core: it holds
no platform defines, so a change to it can't affect one platform alone.

A path belongs to a platform through a path token (a directory or file-name
component such as `win-capture/`, `platform-osx.mm`, `threading-posix.c`) or
an extension (`.m`, `.rc`, ...). POSIX/Unix code also builds on macOS.
"""

from __future__ import annotations

import re

LINUX, MACOS, WINDOWS = "linux", "macos", "windows"

_TOKEN = r"(?:^|[/_.\-])({})(?:[/_.\-]|$)"
_MACOS = re.compile(
    _TOKEN.format("mac|macos|osx|cocoa|apple|darwin|coreaudio|avcapture|syphon|videotoolbox"), re.I
)
_WINDOWS = re.compile(_TOKEN.format("win|win32|windows|wasapi|dshow|d3d9|d3d11|d3d12|dxgi|w32-pthreads"), re.I)
_POSIX = re.compile(_TOKEN.format("posix|unix"), re.I)
_LINUX = re.compile(
    _TOKEN.format("linux|nix|x11|wayland|pipewire|pulse|pulseaudio|alsa|v4l2|freebsd|vaapi|jack|sndio|xshm"), re.I
)
_MACOS_SUFFIXES = (".m", ".mm", ".plist", ".xcconfig", ".entitlements")
_WINDOWS_SUFFIXES = (".rc", ".manifest", ".ps1")
# nix-named files macOS also builds (libobs/cmake/os-macos.cmake).
_MACOS_TOO = {"libobs/util/platform-nix.c"}


def platforms(path: str) -> frozenset[str] | None:
    """The platforms `path` belongs to, or None for core (every platform)."""
    found: set[str] = set()
    if path.startswith("cmake/macos/") or path.endswith(_MACOS_SUFFIXES) or _MACOS.search(path):
        found.add(MACOS)
    if path.startswith("cmake/windows/") or path.endswith(_WINDOWS_SUFFIXES) or _WINDOWS.search(path):
        found.add(WINDOWS)
    if _POSIX.search(path) or path in _MACOS_TOO:
        found |= {LINUX, MACOS}
    elif path.startswith("cmake/linux/") or _LINUX.search(path):
        found.add(LINUX)
    return frozenset(found) or None
