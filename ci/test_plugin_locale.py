#!/usr/bin/env -S uv run --script
"""Plugins that load a default locale ship it, under their own name (#157).

    uv run python -m unittest ci/test_plugin_locale.py

A plugin calling OBS_MODULE_USE_DEFAULT_LOCALE("<name>", "en-US") must pass
its own module name and have data/locale/en-US.ini defining every key it
looks up with a literal obs_module_text("..."). Otherwise every launch warns
"Failed to load 'en-US' text" and the UI shows raw keys.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOCALE = re.compile(r'OBS_MODULE_USE_DEFAULT_LOCALE\(\s*"([^"]+)"\s*,\s*"en-US"\s*\)')
TEXT = re.compile(r'obs_module_text\(\s*"([^"]+)"\s*\)')
KEY = re.compile(r"^\s*([^#;=\s][^=]*?)\s*=", re.M)
SOURCES = {".c", ".cpp", ".cc", ".mm", ".m", ".h", ".hpp"}
# Plugins this check covers; add others as their locales are verified.
CHECKED = ("decklink-captions", "decklink-output-ui")


def sources(plugin: Path) -> list[Path]:
    return [p for p in plugin.rglob("*") if p.suffix in SOURCES and p.is_file()]


class PluginLocaleTest(unittest.TestCase):
    def test_default_locale_is_shipped(self):
        for name in CHECKED:
            plugin = ROOT / "plugins" / name
            text = "\n".join(p.read_text(errors="ignore") for p in sources(plugin))
            declared = DEFAULT_LOCALE.findall(text)
            with self.subTest(plugin=name):
                self.assertEqual(declared, [name], "OBS_MODULE_USE_DEFAULT_LOCALE must name the module")
                ini = plugin / "data" / "locale" / "en-US.ini"
                self.assertTrue(ini.is_file(), f"{ini.relative_to(ROOT)} is missing")
                keys = set(KEY.findall(ini.read_text()))
                missing = sorted(set(TEXT.findall(text)) - keys)
                self.assertEqual(missing, [], "obs_module_text keys without an en-US entry")


if __name__ == "__main__":
    unittest.main()
