"""A shell spawned through ConPTY reaches TerminalCore's text buffer.

This is an integration gate like ``test_boot_frontier.py`` beside it: it needs
the phase-2 PE that links both halves -- the ``src/winconpty`` pseudoconsole
and ``src/cascadia/TerminalCore`` -- and a Wine that can run it. Neither is in
the repository, so when the PE is absent the gate *skips by name* rather than
passing vacuously.

    cmake --build <phase-2 build dir> \\
        --target openterminal_conpty_terminal_core_gate

builds what this reads. What is asserted is not that bytes came out of a pipe
-- that would pass with no terminal in the picture at all -- but that a string
the shell itself produced is in ``Terminal``'s text buffer, read back through
``Terminal::LockForReading`` and ``Terminal::GetTextBuffer``. The negative
control below runs the identical session with the ingestion step removed and
requires it to fail; if that ever passes, this gate is measuring nothing.
"""

import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
RUNNER = REPOSITORY / "phase4" / "scripts" / "conpty_live.py"
EXECUTABLE = Path(os.environ.get(
    "OPENTERMINAL_CONPTY_GATE_EXE",
    "/tmp/openterminal-mingw/native-build/"
    "openterminal_conpty_terminal_core_gate.exe"))
PREFIX = Path(os.environ.get("OPENTERMINAL_CONPTY_PREFIX",
                             "/tmp/openterminal-conpty/prefix"))
MARKER_PREFIX = "CONPTY_GATE_"
MARKER_VALUE = "1138"
MARKER = MARKER_PREFIX + MARKER_VALUE
SESSION_TIMEOUT = 30


class ConptyReachesTerminalCore(unittest.TestCase):
    def setUp(self):
        if not EXECUTABLE.is_file():
            raise unittest.SkipTest(
                f"phase-2 ConPTY gate absent: {EXECUTABLE} does not exist; "
                "cmake --build <build dir> --target "
                "openterminal_conpty_terminal_core_gate")
        if shutil.which("wine") is None:
            raise unittest.SkipTest("wine is not installed")

    def measure(self, ingestion: str) -> dict:
        completed = subprocess.run(
            [sys.executable, "-B", str(RUNNER),
             "--executable", str(EXECUTABLE),
             "--prefix", str(PREFIX),
             "--timeout", str(SESSION_TIMEOUT),
             "--ingestion", ingestion,
             "--marker-value", MARKER_VALUE],
            capture_output=True, text=True, errors="replace",
            timeout=4 * SESSION_TIMEOUT + 120, check=False)
        self.assertTrue(
            completed.stdout.strip(),
            "the runner produced no report:\n" + completed.stderr[-4000:])
        return json.loads(completed.stdout)

    def test_the_shells_output_is_in_the_terminal_buffer(self):
        measured = self.measure("terminal-core")
        session = measured["session"]
        self.assertIsNotNone(
            session, "the gate PE produced no report: " + json.dumps(measured))
        self.assertFalse(measured["timed_out"], json.dumps(measured))

        self.assertTrue(session["pseudoconsole_created"],
                        "no pseudoconsole: " + json.dumps(session))
        self.assertTrue(session["shell_started"],
                        "no shell in the pseudoconsole: " + json.dumps(session))
        self.assertEqual(session["refusal"], "", json.dumps(session))

        self.assertGreater(session["pty_bytes_read"], 0,
                           "the pseudoconsole emitted nothing")
        self.assertGreater(session["wide_chars_written"], 0,
                           "nothing was written into TerminalCore")
        self.assertTrue(
            session["marker_in_terminal_buffer"],
            f"{MARKER} is not in TerminalCore's text buffer:\n"
            + json.dumps(session, indent=1))

        rows = session["terminal_buffer"].split("\n")
        self.assertIn(MARKER, rows[session["marker_row"]],
                      "marker_row does not name the row the marker is on")
        # The marker is the shell's expansion of a variable, so no line that
        # was typed into the pseudoconsole can contain it. If one does, the
        # marker came from the console's input echo and proves nothing.
        for row in rows:
            if row.startswith("set ") or row.startswith("echo "):
                self.assertNotIn(
                    MARKER, row,
                    "the marker appeared on a line that was typed in, not on "
                    "a line the shell produced: " + row)

    def test_without_the_ingestion_step_the_buffer_stays_empty(self):
        measured = self.measure("none")
        session = measured["session"]
        self.assertIsNotNone(
            session, "the gate PE produced no report: " + json.dumps(measured))
        self.assertTrue(session["shell_started"], json.dumps(session))
        self.assertGreater(
            session["pty_bytes_read"], 0,
            "the negative control did not even run a session, so it proves "
            "nothing about the ingestion path")
        self.assertEqual(session["wide_chars_written"], 0, json.dumps(session))
        self.assertFalse(
            session["marker_in_terminal_buffer"],
            "TerminalCore's buffer holds the marker with the ingestion step "
            "removed; this gate is not measuring the ingestion path:\n"
            + json.dumps(session, indent=1))


if __name__ == "__main__":
    unittest.main()
