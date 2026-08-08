"""The boot frontier matches its committed expectation.

This is an integration gate, not a unit test: it needs the phase-2 build of
``WindowsTerminal.exe`` and a Wine prefix with the phase-3 XAML DLL
registered, neither of which is in the repository. When either is absent the
test *skips by name* — it never passes vacuously.

The expectation moves like the oracle digest: when the runtime genuinely
advances the frontier, rerun ``boot_frontier.py``, read the change, and commit
the new document deliberately.
"""

import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
EXPECTED = REPOSITORY / "phase4" / "expected" / "boot-frontier.json"
EXECUTABLE = Path(
    os.environ.get("OPENTERMINAL_TERMINAL_EXE",
                   "/tmp/openterminal-mingw/native-build/WindowsTerminal.exe"))
PREFIX = Path(
    os.environ.get("OPENTERMINAL_XAML_PREFIX",
                   "/tmp/openterminal-xamlcore/wine-prefix"))


class BootFrontier(unittest.TestCase):
    def test_frontier_matches_expectation(self):
        if not EXECUTABLE.is_file():
            raise unittest.SkipTest(
                f"phase-2 build absent: {EXECUTABLE} does not exist")
        if not PREFIX.is_dir():
            raise unittest.SkipTest(
                f"XAML Wine prefix absent: {PREFIX} does not exist")
        for tool in ("wine", "xvfb-run"):
            if shutil.which(tool) is None:
                raise unittest.SkipTest(f"{tool} is not installed")

        completed = subprocess.run(
            [sys.executable, "-B",
             str(REPOSITORY / "phase4" / "scripts" / "boot_frontier.py"),
             "--executable", str(EXECUTABLE),
             "--prefix", str(PREFIX)],
            capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        measured = json.loads(completed.stdout)
        expected = json.loads(EXPECTED.read_text())
        self.assertEqual(
            measured, expected,
            "the boot frontier moved; if the runtime really advanced, "
            "commit the new document to phase4/expected/boot-frontier.json "
            "the way an oracle re-baseline is committed")


if __name__ == "__main__":
    unittest.main()
