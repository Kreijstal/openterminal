"""Run a live ConPTY session under Wine and report what TerminalCore received.

The measurement itself is a PE the phase-2 build links,
``openterminal_conpty_terminal_core_gate.exe`` (``phase2/tests/
conpty_terminal_core_gate.cpp``): it creates a pseudoconsole with the
``src/winconpty`` library, spawns ``cmd.exe`` into it, types a command, feeds
every byte the pseudoconsole emits into a
``Microsoft::Terminal::Core::Terminal``, and reports whether the shell's own
output arrived in that terminal's text buffer. This script is the Wine side:
it provisions a prefix, runs the PE with a hard timeout, and passes its JSON
through with the run's own facts added.

No display is needed -- the console host runs headless and never opens a
window -- so this does not start an X server and cannot collide with one.

    python3 -B phase4/scripts/conpty_live.py \\
        --executable /tmp/openterminal-mingw/native-build/openterminal_conpty_terminal_core_gate.exe \\
        --prefix /tmp/openterminal-conpty/prefix

Wine refuses a prefix whose immediate parent is not owned by the user, so the
prefix must be nested rather than sitting directly under ``/tmp``.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

BUILD_TARGET = "openterminal_conpty_terminal_core_gate"


def boot_prefix(prefix: Path, wine: str, wineboot_timeout: int) -> dict:
    """Make sure the prefix exists and has a console host in it."""
    prefix.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEDEBUG"] = "-all"
    environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    completed = subprocess.run(
        [wine, "wineboot.exe", "-u"], env=environment, capture_output=True,
        text=True, errors="replace", timeout=wineboot_timeout, check=False)
    host = prefix / "drive_c" / "windows" / "system32" / "conhost.exe"
    return {
        "environment": environment,
        "wineboot_exit_code": completed.returncode,
        "console_host_present": host.is_file(),
    }


def run(executable: Path, prefix: Path, wine: str, timeout: int,
        ingestion: str, pty_path: str, marker_value: str) -> dict:
    booted = boot_prefix(prefix, wine, timeout)
    environment = booted.pop("environment")

    command = [
        "timeout", str(timeout + 10), wine, str(executable),
        "--timeout-ms", str(timeout * 1000),
        "--ingestion", ingestion,
        "--pty-path", pty_path,
        "--marker-value", marker_value,
    ]
    completed = subprocess.run(
        command, env=environment, capture_output=True, text=True,
        errors="replace", check=False)

    measured: dict = {
        "wine_loader": wine,
        "prefix": str(prefix),
        "exit_code": completed.returncode,
        "timed_out": completed.returncode == 124,
        **booted,
    }
    try:
        measured["session"] = json.loads(completed.stdout)
    except json.JSONDecodeError:
        # A PE that could not even report is a failure with a name, not a
        # missing key: the caller must be able to tell this apart from a
        # session that ran and found nothing.
        measured["session"] = None
        measured["refusal"] = "gate-produced-no-report"
        measured["stdout"] = completed.stdout[-4000:]
        measured["stderr"] = completed.stderr[-4000:]
    return measured


def build(executable: Path) -> bool:
    """Build the gate PE in the phase-2 build directory that would hold it."""
    build_dir = executable.parent
    if not (build_dir / "CMakeCache.txt").is_file():
        return False
    completed = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", BUILD_TARGET,
         "--parallel"],
        capture_output=True, text=True, errors="replace", check=False)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout[-4000:] + completed.stderr[-4000:])
    return executable.is_file()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--executable", type=Path,
        default=Path("/tmp/openterminal-mingw/native-build/"
                     f"{BUILD_TARGET}.exe"),
        help="the phase-2 built ConPTY/TerminalCore gate PE")
    parser.add_argument("--prefix", type=Path, required=True,
                        help="Wine prefix to boot and run in (must be nested, "
                             "not directly under /tmp)")
    parser.add_argument("--wine-loader", type=Path,
                        help="exact Wine loader (default: wine from PATH)")
    parser.add_argument("--timeout", type=int, default=30,
                        help="seconds the session may take to produce the marker")
    parser.add_argument("--ingestion", default="terminal-core",
                        choices=("terminal-core", "none"),
                        help="'none' is the negative control: read the "
                             "pseudoconsole but never write to the terminal")
    parser.add_argument("--pty-path", default="auto",
                        choices=("auto", "create", "pack"),
                        help="which winconpty pseudoconsole entry point to use")
    parser.add_argument("--marker-value", default="1138")
    parser.add_argument("--build", action="store_true",
                        help="build the gate PE first if it is missing")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()

    if not arguments.executable.is_file() and arguments.build:
        build(arguments.executable)
    if not arguments.executable.is_file():
        raise SystemExit(
            f"{arguments.executable} does not exist; build phase 2 "
            f"(cmake --build <build dir> --target {BUILD_TARGET})")

    wine = str(arguments.wine_loader) if arguments.wine_loader else shutil.which("wine")
    if wine is None:
        raise SystemExit("wine is not installed")

    measured = run(arguments.executable, arguments.prefix, wine,
                   arguments.timeout, arguments.ingestion,
                   arguments.pty_path, arguments.marker_value)
    text = json.dumps(measured, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(text)
    sys.stdout.write(text)
    session = measured.get("session")
    return 0 if session and session.get("marker_in_terminal_buffer") else 1


if __name__ == "__main__":
    raise SystemExit(main())
