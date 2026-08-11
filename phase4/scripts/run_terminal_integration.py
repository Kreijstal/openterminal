#!/usr/bin/env python3
"""Register one exact OpenXaml build and launch one exact Terminal build.

This is the artifact-coupled integration gate. It never reuses a Wine prefix:
the supplied ``openxaml.dll`` is registered in a newly created prefix, the
Terminal executable is launched from its own deployment directory with that
directory as the XBF root. Success requires both staying alive until the
bounded timeout and a clean, non-empty authored frame committed through the
selected presenter. Generated registry data, the prefix, and logs stay under
``/tmp`` (or the explicitly supplied temporary prefix); no binary is copied
into the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))
from build_xamlcore import registration  # noqa: E402
from install_deployment_fonts import install as install_deployment_fonts  # noqa: E402
from prepare_runtime_fonts import (  # noqa: E402
    DEFAULT_SPEC as DEFAULT_RUNTIME_FONT_SPEC,
    prepare as prepare_runtime_fonts,
)

IMPORT_FAILURE = re.compile(r"import_dll Library (\S+) \(which is needed")
MISSING_CLASS = re.compile(r'RoGetActivationFactory Failed to find library for L"([^"]+)"')
TERMINATED = re.compile(r"terminate called after throwing an instance of '([^']+)'")
OPENXAML_NOT_IMPLEMENTED = re.compile(r"OpenXaml: E_NOTIMPL ([A-Za-z0-9_.]+)")
OPENXAML_XBF_FAILURE = re.compile(r"OpenXaml: XBF[^\r\n\"]*failed[^\r\n\"]*")
ACCESS_VIOLATION = re.compile(
    r"(?:Unhandled exception|Exception)\s+0xc0000005|Unhandled page fault",
    re.I,
)
NO_WINDOW_DRIVER = "nodrv_CreateWindow"
FRAME_EVENT = re.compile(r"OpenXaml frame event=(commit|present) ([^\"\r\n]*)")
SCENE_STATS = re.compile(r"OpenXaml frame event=scene-stats ([^\"\r\n]*)")
DEFAULT_TERMINAL_ARGUMENTS = ("cmd.exe", "/k")


def _integer_field(record: str, name: str) -> int | None:
    match = re.search(rf"(?:^| ){re.escape(name)}=(\d+)(?: |$)", record)
    return int(match.group(1)) if match else None


def _text_field(record: str, name: str) -> str | None:
    match = re.search(rf"(?:^| ){re.escape(name)}=([^ ]+)(?: |$)", record)
    return match.group(1) if match else None


def frame_evidence(log: str) -> dict:
    """Return the strongest completed authored-frame transaction in *log*.

    The transparent one-pixel attach/probe is deliberately excluded. A CPU
    frame is presented only after its matching WM_PAINT record; a successful
    DirectComposition Commit is itself the atomic compositor publication and
    additionally needs non-empty retained-scene statistics.
    """
    records = [(event, body) for event, body in FRAME_EVENT.findall(log)]
    presented_cpu_generations = {
        _integer_field(body, "generation")
        for event, body in records if event == "present"
    }
    dcomp_stats: dict[int, tuple[int, int]] = {}
    for body in SCENE_STATS.findall(log):
        if _text_field(body, "backend") != "dcomp":
            continue
        generation = _integer_field(body, "generation")
        nodes = _integer_field(body, "nodes")
        commands = _integer_field(body, "commands")
        if generation is not None and nodes is not None and commands is not None:
            dcomp_stats[generation] = (nodes, commands)

    candidates = []
    for event, body in records:
        if event != "commit":
            continue
        reason = _text_field(body, "reason")
        generation = _integer_field(body, "generation")
        extent = re.search(r"(?:^| )extent=(\d+)x(\d+)(?: |$)", body)
        if (reason in (None, "attach") or generation is None or not extent):
            continue
        width, height = int(extent.group(1)), int(extent.group(2))
        if width <= 1 or height <= 1:
            continue
        backend = _text_field(body, "backend") or "cpu"
        refusals = _integer_field(body, "refusals")
        text_failures = _integer_field(body, "text_failures") or 0
        render_issues = _integer_field(body, "render_issues") or 0
        dcomp_issues = _integer_field(body, "dcomp_issues") or 0
        if backend == "dcomp":
            nodes, commands = dcomp_stats.get(generation, (0, 0))
            presented = nodes > 1 and commands > 0
            opaque = None
        else:
            nodes = commands = None
            presented = generation in presented_cpu_generations
            opaque = _text_field(body, "transparency") == "false"
        clean = (refusals == 0 and text_failures == 0 and
                 render_issues == 0 and dcomp_issues == 0)
        candidates.append({
            "frame_backend": backend,
            "frame_clean": clean,
            "frame_commands": commands,
            "frame_committed": True,
            "frame_dcomp_issues": dcomp_issues,
            "frame_extent": f"{width}x{height}",
            "frame_generation": generation,
            "frame_nodes": nodes,
            "frame_opaque": opaque,
            "frame_presented": presented,
            "frame_refusals": refusals,
            "frame_render_issues": render_issues,
            "frame_text_failures": text_failures,
        })

    if candidates:
        return candidates[-1]
    return {
        "frame_backend": None,
        "frame_clean": False,
        "frame_commands": None,
        "frame_committed": False,
        "frame_dcomp_issues": None,
        "frame_extent": None,
        "frame_generation": None,
        "frame_nodes": None,
        "frame_opaque": None,
        "frame_presented": False,
        "frame_refusals": None,
        "frame_render_issues": None,
        "frame_text_failures": None,
    }


def require_file(path: Path, description: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise SystemExit(f"{description} does not exist: {resolved}")
    return resolved


def require_tool(name: str) -> str:
    located = shutil.which(name)
    if not located:
        raise SystemExit(f"required tool is not on PATH: {name}")
    return located


def wine_path(path: Path) -> str:
    return "Z:" + str(path.resolve()).replace("/", "\\")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def find_mingw_runtime() -> Path:
    compiler = Path(require_tool("x86_64-w64-mingw32-g++")).resolve()
    triple = subprocess.run(
        [str(compiler), "-dumpmachine"], capture_output=True, text=True,
        check=True).stdout.strip()
    printed = subprocess.run(
        [str(compiler), "-print-file-name=libstdc++-6.dll"], capture_output=True,
        text=True, check=True).stdout.strip()
    candidates = []
    printed_path = Path(printed)
    if printed_path.is_absolute() and printed_path.is_file():
        candidates.append(printed_path.parent)
    candidates.extend((
        compiler.parent,
        compiler.parent.parent / triple / "bin",
        Path("/usr") / triple / "bin",
    ))
    required = ("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")
    for runtime in candidates:
        if all((runtime / name).is_file() for name in required):
            return runtime.resolve()
    raise SystemExit("could not locate one MinGW runtime directory containing "
                     + ", ".join(required))


def evaluate(returncode: int, log: str, timeout_seconds: int) -> dict:
    missing_imports = sorted(set(IMPORT_FAILURE.findall(log)))
    missing_classes = []
    for name in MISSING_CLASS.findall(log):
        if name not in missing_classes:
            missing_classes.append(name)
    terminated = TERMINATED.search(log)
    not_implemented = OPENXAML_NOT_IMPLEMENTED.search(log)
    xbf_failure = OPENXAML_XBF_FAILURE.search(log)
    access_violation = bool(ACCESS_VIOLATION.search(log))
    window_driver_missing = NO_WINDOW_DRIVER in log
    timed_out = returncode == 124
    frame = frame_evidence(log)
    blockers = bool(missing_imports or missing_classes or terminated or not_implemented or
                    xbf_failure or access_violation or window_driver_missing or
                    not frame["frame_committed"] or
                    not frame["frame_presented"] or
                    not frame["frame_clean"])
    result = {
        "access_violation": access_violation,
        "crash": terminated.group(1) if terminated else None,
        "exit_code": returncode,
        "first_missing_class": missing_classes[0] if missing_classes else None,
        "first_not_implemented": not_implemented.group(1) if not_implemented else None,
        "first_xbf_failure": xbf_failure.group(0) if xbf_failure else None,
        "missing_imports": missing_imports,
        "timeout_seconds": timeout_seconds,
        "timed_out": timed_out,
        "window_driver_present": not window_driver_missing,
        "success": timed_out and not blockers,
    }
    result.update(frame)
    return result


def ensure_fresh_prefix(prefix: Path) -> Path:
    resolved = prefix.resolve()
    temporary_root = Path(tempfile.gettempdir()).resolve()
    if resolved == temporary_root or temporary_root not in resolved.parents:
        raise SystemExit(f"Wine prefix must be a child of {temporary_root}: {resolved}")
    if resolved.exists():
        if not resolved.is_dir() or any(resolved.iterdir()):
            raise SystemExit(f"Wine prefix must be fresh and empty: {resolved}")
    resolved.mkdir(parents=True, exist_ok=True)
    return resolved


def run(arguments: list[str], environment: dict[str, str], cwd: Path | None = None,
        capture: bool = False) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(arguments), flush=True)
    return subprocess.run(arguments, env=environment, cwd=cwd, check=not capture,
                          capture_output=capture, text=True, errors="replace")


def integrate(dll: Path, executable: Path, xbf_root: Path, prefix: Path,
              timeout_seconds: int, log_path: Path, wine: str,
              runtime_font_spec: Path, runtime_font_cache: Path,
              terminal_arguments: list[str]) -> dict:
    require_tool("timeout")
    runtime = find_mingw_runtime()

    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEARCH"] = "win64"
    environment["WINEDEBUG"] = "err+all,warn+combase,warn+debugstr,fixme+combase"
    environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    environment["WINEPATH"] = wine_path(runtime)
    environment["OPENXAML_XBF_ROOT"] = wine_path(xbf_root)
    environment["OPENXAML_TRACE_FRAMES"] = "1"
    font_alias_manifest = prepare_runtime_fonts(
        runtime_font_spec, runtime_font_cache, "wine")
    environment["OPENXAML_FONT_ALIAS_MANIFEST"] = wine_path(font_alias_manifest)

    # Invoke every PE helper through the selected loader. This is important for
    # source-tree Wine builds: using the host wineboot/reg tools would create a
    # prefix with a different server protocol and different builtin DLLs.
    run([wine, "wineboot.exe", "-u"], environment)
    registry_file = prefix / "openxaml-exact.reg"
    registry_file.write_text(registration(dll), encoding="utf-8", newline="\n")
    run([wine, "regedit.exe", str(registry_file)], environment)

    # Terminal loads the fonts it deploys through a DirectWrite call Wine
    # stubs, so install them the way Wine does resolve fonts. Without this
    # AtlasEngine's UpdateFont fails silently and the boot dies dividing by a
    # zero cell size. See install_deployment_fonts.py.
    deployment_fonts = install_deployment_fonts(
        executable.parent, prefix, environment, wine)

    # Prove the registration refers to the exact input artifact before launch.
    query = run(
        [wine, "reg.exe", "query",
         r"HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId\Windows.UI.Xaml.Application",
         "/v", "DllPath"], environment, capture=True)
    expected_dll = wine_path(dll)
    if query.returncode != 0 or expected_dll.lower() not in query.stdout.lower():
        raise SystemExit("fresh prefix does not resolve Windows.UI.Xaml.Application "
                         f"to the requested DLL: {expected_dll}\n{query.stdout}{query.stderr}")

    command = ["timeout", "--signal=TERM", "--kill-after=5", str(timeout_seconds),
               wine, "./" + executable.name, *terminal_arguments]
    completed = run(command, environment, cwd=executable.parent, capture=True)
    log = completed.stderr + completed.stdout
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(log, encoding="utf-8", errors="replace", newline="\n")
    result = evaluate(completed.returncode, log, timeout_seconds)
    result.update({
        "executable": str(executable),
        "executable_sha256": sha256(executable),
        "openxaml_dll": str(dll),
        "openxaml_dll_sha256": sha256(dll),
        "deployment_fonts": deployment_fonts["fonts"],
        "prefix": str(prefix),
        "runtime_font_alias_manifest": str(font_alias_manifest),
        "runtime_font_alias_manifest_sha256": sha256(font_alias_manifest),
        "runtime_font_spec": str(runtime_font_spec),
        "runtime_font_spec_sha256": sha256(runtime_font_spec),
        "wine_loader": wine,
        "xbf_root": str(xbf_root),
        "terminal_arguments": terminal_arguments,
    })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xaml-dll", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--xbf-root", type=Path,
                        help="deployed XBF root (default: executable directory)")
    parser.add_argument("--prefix", type=Path,
                        help="fresh prefix below /tmp (default: temporary prefix)")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument(
        "--terminal-argument", action="append", dest="terminal_arguments",
        help=("argument passed to WindowsTerminal.exe; repeat for multiple "
              "arguments (default: cmd.exe /k, a shell Wine provides)"))
    parser.add_argument("--wine-loader", type=Path,
                        help="exact Wine loader (default: wine from PATH)")
    parser.add_argument("--wineserver", type=Path,
                        help="matching wineserver used for cleanup (default: wineserver from PATH)")
    parser.add_argument("--log", type=Path,
                        help="launch log (default: <prefix>/terminal-integration.log)")
    parser.add_argument("--runtime-font-spec", type=Path,
                        default=DEFAULT_RUNTIME_FONT_SPEC,
                        help="pinned runtime-font specification")
    parser.add_argument("--runtime-font-cache", type=Path,
                        default=Path(tempfile.gettempdir()) /
                                "openterminal-runtime-fonts",
                        help="temporary verified font cache")
    parser.add_argument("--no-xvfb", action="store_true")
    arguments = parser.parse_args()
    if arguments.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    # One X server must cover prefix initialization, registration and launch;
    # separate xvfb-run calls leave Wine background processes referring to a
    # display which has already gone away. Re-exec the whole command once
    # inside Xvfb when the caller has no display.
    if not arguments.no_xvfb and not os.environ.get("DISPLAY"):
        xvfb_run = require_tool("xvfb-run")
        command = [xvfb_run, "--auto-servernum", sys.executable, "-B",
                   str(Path(__file__).resolve()), *sys.argv[1:], "--no-xvfb"]
        return subprocess.run(command, check=False).returncode

    dll = require_file(arguments.xaml_dll, "OpenXaml DLL")
    executable = require_file(arguments.executable, "Terminal executable")
    xbf_root = (arguments.xbf_root or executable.parent).resolve()
    if not xbf_root.is_dir():
        raise SystemExit(f"XBF root does not exist: {xbf_root}")
    runtime_font_spec = require_file(
        arguments.runtime_font_spec, "runtime font specification")
    runtime_font_cache = arguments.runtime_font_cache.resolve()
    temporary_root = Path(tempfile.gettempdir()).resolve()
    if (runtime_font_cache == temporary_root or
            temporary_root not in runtime_font_cache.parents):
        raise SystemExit("runtime font cache must be a child of " +
                         str(temporary_root))

    wine = (str(require_file(arguments.wine_loader, "Wine loader"))
            if arguments.wine_loader else require_tool("wine"))
    wineserver = (str(require_file(arguments.wineserver, "wineserver"))
                  if arguments.wineserver else require_tool("wineserver"))

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if arguments.prefix:
        prefix = ensure_fresh_prefix(arguments.prefix)
    else:
        temporary = tempfile.TemporaryDirectory(prefix="openterminal-integration-")
        prefix = Path(temporary.name).resolve()
    log_path = (arguments.log.resolve() if arguments.log
                else prefix / "terminal-integration.log")
    try:
        terminal_arguments = (arguments.terminal_arguments
                              if arguments.terminal_arguments is not None
                              else list(DEFAULT_TERMINAL_ARGUMENTS))
        result = integrate(dll, executable, xbf_root, prefix, arguments.timeout,
                           log_path, wine, runtime_font_spec,
                           runtime_font_cache, terminal_arguments)
        print(json.dumps(result, indent=2, sort_keys=True))
        if not result["success"]:
            print(f"integration log: {log_path}", file=sys.stderr)
            return 1
        return 0
    finally:
        subprocess.run([wineserver, "-k"],
                       env={**os.environ, "WINEPREFIX": str(prefix)}, check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
