"""Boot the mingw-built Terminal host under Wine and report the frontier.

The phase-2 build links a real ``WindowsTerminal.exe`` and phase 3 supplies a
real ``windows.ui.xaml.dll``; this script is the honest meeting point. It runs
the executable in a Wine prefix where the open XAML DLL is registered, under a
virtual display, and reduces the run to a small JSON document: which boot
milestones were reached and the first named thing that stopped the process.

The frontier is a measurement, not a wish. It advances only when the runtime
actually implements what the binary asks for next, and the committed
expectation in ``phase4/expected/boot-frontier.json`` moves the same way the
oracle digest does: deliberately, after reading what changed.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

MISSING_CLASS = re.compile(
    r"RoGetActivationFactory Failed to find library for L\"([^\"]+)\"")
SEMI_STUB = re.compile(
    r"RoGetActivationFactory \(L\"([^\"]+)\", \{([0-9a-fA-F-]+)\}")
IMPORT_FAILURE = re.compile(r"import_dll Library (\S+) \(which is needed")
TERMINATED = re.compile(r"terminate called after throwing an instance of '([^']+)'")
NO_DRIVER = "nodrv_CreateWindow"


def find_mingw_runtime() -> Path:
    for candidate in (
        Path("/usr/x86_64-w64-mingw32/bin"),
        Path("/usr/lib/gcc/x86_64-w64-mingw32"),
    ):
        if (candidate / "libstdc++-6.dll").exists():
            return candidate
    located = shutil.which("x86_64-w64-mingw32-g++")
    if located:
        probe = subprocess.run(
            ["x86_64-w64-mingw32-g++", "-print-file-name=libstdc++-6.dll"],
            capture_output=True, text=True, check=False)
        path = Path(probe.stdout.strip())
        if path.is_file():
            return path.parent
    raise SystemExit("no mingw-w64 runtime DLLs found; install the toolchain")


def run(executable: Path, prefix: Path, timeout: int, use_xvfb: bool) -> dict:
    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEDEBUG"] = "err+all,warn+combase,fixme+combase"
    runtime = find_mingw_runtime()
    environment["WINEPATH"] = "Z:" + str(runtime).replace("/", "\\")

    command = ["wine", str(executable)]
    if use_xvfb:
        command = ["xvfb-run", "--auto-servernum", "timeout", str(timeout)] + command
    else:
        command = ["timeout", str(timeout)] + command
    completed = subprocess.run(
        command, env=environment, capture_output=True, text=True,
        errors="replace", check=False)
    log = completed.stderr + completed.stdout

    missing_imports = sorted(set(IMPORT_FAILURE.findall(log)))
    missing_classes = []
    for name in MISSING_CLASS.findall(log):
        if name not in missing_classes:
            missing_classes.append(name)
    crash = TERMINATED.search(log)

    frontier = {
        "executable": executable.name,
        "exit_code": completed.returncode,
        "milestones": {
            "process_started": bool(log) or completed.returncode == 0,
            "mingw_runtime_loaded": not missing_imports,
            "window_driver_present": NO_DRIVER not in log,
            "reached_winrt_activation": bool(SEMI_STUB.search(log))
            or bool(missing_classes),
        },
        "missing_imports": missing_imports,
        "first_missing_class": missing_classes[0] if missing_classes else None,
        "missing_classes": missing_classes,
        "crash": crash.group(1) if crash else None,
    }
    return frontier


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True,
                        help="the phase-2 built WindowsTerminal.exe")
    parser.add_argument("--prefix", type=Path, required=True,
                        help="Wine prefix with the open XAML DLL registered")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--no-xvfb", action="store_true",
                        help="run against the real display instead of Xvfb")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()

    if not arguments.executable.is_file():
        raise SystemExit(f"{arguments.executable} does not exist; "
                         "build phase 2 first")
    if not arguments.prefix.is_dir():
        raise SystemExit(f"{arguments.prefix} is not a Wine prefix; "
                         "run phase3/scripts/build_xamlcore.py first")

    frontier = run(arguments.executable, arguments.prefix,
                   arguments.timeout, not arguments.no_xvfb)
    text = json.dumps(frontier, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
