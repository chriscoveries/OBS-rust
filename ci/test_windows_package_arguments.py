#!/usr/bin/env -S uv run --script
"""Execute the package action's real PowerShell argument builder (#629, #631).

    uv run python -m unittest ci/test_windows_package_arguments.py
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class WindowsPackageArguments(unittest.TestCase):
    def test_native_and_cross_package_checks_receive_valid_argument_vectors(self):
        if shutil.which("pwsh"):
            engine = ["pwsh"]
        elif shutil.which("nix"):
            engine = ["nix", "shell", "nixpkgs#powershell", "--command", "pwsh"]
        else:
            self.skipTest("PowerShell is unavailable")
        action = (ROOT / ".github/actions/package-obs/action.yaml").read_text()
        block = action.split("    - name: Verify Windows package", 1)[1].split("\n    - ", 1)[0]
        source = textwrap.dedent(block.split("      run: |\n", 1)[1])
        version = "0.1.0-beta.20261014"
        for target in ("x64", "arm64"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                package = root / f"build_{target}/obs-studio-test-windows-{target}.zip"
                package.parent.mkdir()
                package.touch()
                capture = root / "capture.py"
                capture.write_text(
                    "#!/usr/bin/env -S uv run --script\n"
                    "import json, sys\nprint(json.dumps(sys.argv[1:]))\n"
                )
                script = root / "check.ps1"
                capture_literal = "'" + capture.as_posix().replace("'", "''") + "'"
                script.write_text(source.replace("${{ inputs.target }}", target)
                                  .replace("ci/windows_zip_check.py", capture_literal))
                env = dict(os.environ, OBS_RUST_VERSION=version)
                result = subprocess.run(
                    [*engine, "-NoLogo", "-NoProfile", "-File", str(script)],
                    cwd=root, env=env, capture_output=True, text=True, timeout=120,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                args = json.loads(result.stdout.splitlines()[-1])
                expected = ["check", str(package)]
                if target == "x64":
                    expected += ["--run", "--expect-version", version]
                self.assertEqual(args, expected)


if __name__ == "__main__":
    unittest.main()
