"""A keystroke typed into the real terminal window reaches the shell.

This is an integration gate like ``test_boot_frontier.py`` and
``test_check_xbf_ui_render.py`` beside it: it needs the phase-2 build of
``WindowsTerminal.exe``, a built ``openxaml.dll``, the phase-3
``terminal_input_probe.exe``, a Wine that can run them and an X server. None of
those is in the repository, so when any is absent this *skips by name* rather
than passing vacuously.

    python3 -B phase3/scripts/build_xamlcore.py --root /tmp/openterminal-xamlcore

builds the probe and the DLL this reads.

What is asserted is not that keystrokes were injected -- that would pass with
no terminal in the picture -- but that the shell the terminal started executed
the command they spell and wrote its sentinel file inside the prefix. The
negative control runs the identical session with the injection removed and
requires no sentinel; if that ever produces one, this gate is measuring
nothing.

Both runs get their own fresh prefix, so neither can see the other's files.

Measured on 2026-08-10, fixed loader at wine 8d664853bd5, fresh prefix, Xvfb:
the hosting window is created at its full client size (1113x627) and never
gains WS_VISIBLE -- its style stays 0x04CF0000 for the whole run. Terminal
calls ``ShowWindow`` in exactly one place, ``AppHost::_WindowInitializedHandler``
, and only after ``co_await wil::resume_foreground(CoreDispatcher, Low)``. That
queue never drains because the UI thread blocks forever inside
``Microsoft::Console::Render::Renderer::TriggerTeardown`` -- confirmed by a
filtered Wine relay trace showing ``WaitForSingleObject(handle, INFINITE)``
returning into ``renderer.cpp`` and never returning, with the render thread it
waits on spinning rather than exiting. With no visible window the desktop has
nowhere to deliver ``SendInput`` to, and messages posted straight to the
island's child window are never pumped either: the runtime's own
``OPENXAML_TRACE_INPUT`` record shows the island gaining host focus and then
receiving no key or character message at all.

The same condition makes ``check_xbf_ui_render.py``'s ``probe-observed-window``
check fail on the same loader, so it is not a property of this gate.

Addendum, 2026-08-11: that window is now shown, the keystrokes now arrive, and
this file therefore asserts them. Two defects stood between the two states, and
both were found by bisecting the one difference between this gate and the pane
gate -- the settings document -- one field at a time:

  * the tab closed about a second after startup for *every* profile without
    ``closeOnExit: never``, including the pane gate's own. The shell really did
    start (its readiness file was written) and really did exit at once, because
    the console host serving its pseudoconsole had already gone: Windows
    Terminal's ``winconpty`` starts ``conhost.exe`` with
    ``--textMeasurement graphemes``, and Wine's console host answered an option
    it did not recognise by returning from ``wmain`` before its main loop
    (``programs/conhost/conhost.c``). A shell attached to a console nobody
    serves reads end of file from its own input and exits, the connection
    closes, the last tab closes, and the process exits 0 -- which is what
    ``closeOnExit: never`` had been hiding. Fixed in Wine by naming the
    unimplemented options and serving the console anyway;

  * with the shell alive the window is shown and the keystrokes are delivered,
    and the first one faulted the process on a null read inside
    ``TermControl::_GetPressedModifierKeys`` (symbolised from the faulting
    address). That method reads seven modifier keys from
    ``CoreWindow::GetForCurrentThread()`` and is ``noexcept``, so it neither
    checks for null nor could survive an exception: on Windows a XAML island
    thread always has a CoreWindow. This runtime did not implement the class at
    all, so it resolved to a stub that answered "success, here is nothing".
    ``Windows.UI.Core.CoreWindow`` is now the runtime's own, answering the
    thread's real key and pointer state.

With both fixed, on the canonical loader and a fresh prefix, the shell executes
the typed command and the pane paints it: 44 keystrokes injected, 88 key and 44
character messages received by the island with a focused sink, the sentinel file
reading ``INPUT_GATE_1138``, and 42 of 63 sampled points inside the window
different after typing.

The conhost half of the fix lives in Wine, not here, so which loader runs the
session decides whether a shell can live at all. When ``OPENTERMINAL_WINE_LOADER``
names a loader, that loader is what this gate speaks for and everything is
asserted. When it does not, the session runs on whatever ``wine`` is on PATH --
measured on 2026-08-11, stock wine-11.13 still kills the console host over
``--textMeasurement`` and the shell never starts (``ready=False waits=0``,
terminal exits 0 in about a second). That one measured signature, on the
unnamed loader only, is skipped by name below -- the same shape as the layered
map-survival checks, which enforce on the fixed loader and name the stock
loader's defect instead of failing on it. A named loader never skips: if the
canonical loader ever shows this signature again, that is a regression and it
fails.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
RUNNER = REPOSITORY / "phase4" / "scripts" / "check_input_roundtrip.py"

EXECUTABLE = Path(os.environ.get(
    "OPENTERMINAL_TERMINAL_EXE",
    "/tmp/openterminal-mingw/native-build/WindowsTerminal.exe"))
XAML_DLL = Path(os.environ.get("OPENTERMINAL_XAML_DLL",
                               "/tmp/openterminal-xamlcore/openxaml.dll"))
INPUT_PROBE = Path(os.environ.get(
    "OPENTERMINAL_INPUT_PROBE",
    "/tmp/openterminal-xamlcore/terminal_input_probe.exe"))
WINE_LOADER = os.environ.get("OPENTERMINAL_WINE_LOADER")
WINESERVER = os.environ.get("OPENTERMINAL_WINESERVER")
MECHANISM = os.environ.get("OPENTERMINAL_INPUT_MECHANISM", "sendinput")
# A nested directory: Wine refuses a prefix whose immediate parent is not the
# user's own, which rules out one directly under /tmp.
PREFIX_ROOT = Path(os.environ.get("OPENTERMINAL_INPUT_PREFIX_ROOT",
                                  str(Path(tempfile.gettempdir()) /
                                      "openterminal-input-roundtrip")))
TIMEOUT = int(os.environ.get("OPENTERMINAL_INPUT_TIMEOUT", "90"))


class InputReachesTheShell(unittest.TestCase):
    def setUp(self):
        for path, description in (
                (EXECUTABLE, "phase-2 Terminal build"),
                (XAML_DLL, "phase-3 openxaml.dll"),
                (INPUT_PROBE, "phase-3 terminal_input_probe.exe")):
            if not path.is_file():
                raise unittest.SkipTest(
                    f"{description} absent: {path} does not exist")
        for tool in ("wine", "xvfb-run", "timeout"):
            if shutil.which(tool) is None:
                raise unittest.SkipTest(f"{tool} is not installed")

    def measure(self, mechanism: str, nonce: str) -> dict:
        prefix = PREFIX_ROOT / f"prefix-{mechanism}-{nonce}"
        if prefix.exists():
            shutil.rmtree(prefix)
        prefix.mkdir(parents=True)
        command = [sys.executable, "-B", str(RUNNER),
                   "--xaml-dll", str(XAML_DLL),
                   "--executable", str(EXECUTABLE),
                   "--input-probe", str(INPUT_PROBE),
                   "--prefix", str(prefix),
                   "--mechanism", mechanism,
                   "--nonce", nonce,
                   "--timeout", str(TIMEOUT)]
        if WINE_LOADER:
            command += ["--wine-loader", WINE_LOADER]
        if WINESERVER:
            command += ["--wineserver", WINESERVER]
        completed = subprocess.run(
            command, capture_output=True, text=True, errors="replace",
            timeout=6 * TIMEOUT + 300, check=False)
        self.assertTrue(
            completed.stdout.strip(),
            "the runner produced no report:\n" + completed.stderr[-4000:])
        return json.loads(completed.stdout)

    def require_a_live_shell(self, checks: dict) -> None:
        """Skips by name on the one measured stock-loader signature.

        Only when no loader was named: an explicitly requested loader is the
        thing this gate speaks for, and a dead shell there is a failure, not
        an excuse. The signature is the shell's own readiness file never
        appearing -- written by the profile's command line through winconpty
        and Wine's conhost before any of our code is in the path -- which is
        what a console host that exits over ``--textMeasurement`` produces.
        """
        shell = checks["shell-started"]
        if shell["status"] == "pass" or WINE_LOADER:
            return
        raise unittest.SkipTest(
            "skipped by name after measurement: the PATH wine's console host "
            "does not survive winconpty's --textMeasurement (fixed in "
            "kreijstal-fixes ef5d4f6b758), so no shell can start under it: "
            + json.dumps(shell) + "; set OPENTERMINAL_WINE_LOADER to a fixed "
            "loader to enforce this gate")

    def test_typed_keystrokes_make_the_shell_write_the_sentinel(self):
        measured = self.measure(MECHANISM, "1138")
        checks = {check["name"]: check for check in measured["checks"]}

        self.assertTrue(checks["probe-observed-window"]["status"] == "pass",
                        json.dumps(checks["probe-observed-window"]))
        self.require_a_live_shell(checks)
        self.assertTrue(checks["shell-started"]["status"] == "pass",
                        json.dumps(checks["shell-started"]))
        self.assertTrue(checks["keystrokes-injected"]["status"] == "pass",
                        json.dumps(checks["keystrokes-injected"]))

        # The window is shown and the keystrokes arrive; there is nothing left
        # to excuse. What used to stand here was a skip for the one measured
        # condition under which no keystroke could be delivered at all -- the
        # hosting window never gaining WS_VISIBLE. That condition is gone, so
        # the checks it covered are asserted below like every other.
        self.assertEqual(
            checks["hosting-window-visible"]["status"], "pass",
            json.dumps(checks["hosting-window-visible"]))
        self.assertEqual(
            checks["keystrokes-reached-the-island"]["status"], "pass",
            "the keystrokes never reached the island:\n"
            + json.dumps(checks["keystrokes-reached-the-island"], indent=1))
        self.assertEqual(
            checks["sentinel-content-matches"]["status"], "pass",
            "the shell did not run the command that was typed into the "
            "window:\n" + json.dumps(measured["checks"], indent=1))
        self.assertTrue(measured["success"],
                        "failed checks: " + json.dumps(measured["failed"]))

    def test_without_injection_the_shell_writes_nothing(self):
        measured = self.measure("none", "2276")
        checks = {check["name"]: check for check in measured["checks"]}
        # The control has to have got as far as the real run did, or its
        # silence says nothing about the injection path it removed.
        self.assertEqual(checks["probe-observed-window"]["status"], "pass",
                         json.dumps(checks["probe-observed-window"]))
        self.require_a_live_shell(checks)
        self.assertEqual(checks["shell-started"]["status"], "pass",
                         json.dumps(checks["shell-started"]))
        self.assertEqual(checks["no-keystrokes-injected"]["status"], "pass",
                         json.dumps(checks["no-keystrokes-injected"]))
        self.assertEqual(
            checks["no-sentinel-without-injection"]["status"], "pass",
            "a sentinel appeared with the injection step removed; this gate "
            "is not measuring the input path:\n"
            + json.dumps(measured["checks"], indent=1))


if __name__ == "__main__":
    unittest.main()
