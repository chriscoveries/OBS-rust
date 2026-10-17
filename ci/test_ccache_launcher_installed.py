#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["pyyaml"]
# ///
"""A workflow step that builds with `ccache` as the compiler launcher runs
after ccache is installed.

#957 moved the main Linux builds to zccache and dropped ccache from
rust/tools/linux-validate/apt-packages.txt; the Fast Gate's "Build Touched
Linux Targets" step still configured -DCMAKE_C_COMPILER_LAUNCHER=ccache and
failed every plugin PR with "ccache: not found".
"""

from pathlib import Path
import re
import unittest

import yaml

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github/workflows/build-project.yaml"
APT_LIST = ROOT / "rust/tools/linux-validate/apt-packages.txt"
# Scripts a step runs that configure a ccache launcher themselves.
CCACHE_SCRIPTS = ("ci/linux_qt_frontend.sh",)


def uses_ccache(run: str) -> bool:
    return bool(re.search(r"COMPILER_LAUNCHER=ccache\b", run)) or any(s in run for s in CCACHE_SCRIPTS)


def installs_ccache(run: str) -> bool:
    apt_list = [
        line.strip() for line in APT_LIST.read_text().splitlines() if line.strip() and not line.startswith("#")
    ]
    # An apt-get install command (with its continuation lines) naming ccache.
    joined = run.replace("\\\n", " ")
    for line in joined.splitlines():
        if "apt-get install" in line and re.search(r"(?<![\w-])ccache(?![\w-])", line):
            return True
    return "apt-packages.txt" in run and "ccache" in apt_list


class CcacheLauncherTest(unittest.TestCase):
    def test_ccache_is_installed_before_it_launches_a_build(self):
        workflow = yaml.safe_load(WORKFLOW.read_text())
        checked = 0
        for job_name, job in workflow["jobs"].items():
            installed = False
            for step in job.get("steps", []):
                run = step.get("run", "")
                installed = installed or installs_ccache(run)
                if uses_ccache(run):
                    checked += 1
                    with self.subTest(job=job_name, step=step.get("name")):
                        self.assertTrue(installed, "install ccache before configuring it as the launcher")
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main()
