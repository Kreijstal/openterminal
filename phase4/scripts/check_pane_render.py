#!/usr/bin/env python3
"""Prove that the terminal pane shows the pixels TermControl produced.

The XBF gate answers "is the application's own markup on the screen". It says
nothing about the one region of the window that XAML does not draw: the pane.
TermControl renders its text with AtlasEngine into a swap chain of its own and
hands that swap chain to the XAML tree; every pixel inside the pane comes from
there. A window whose chrome is perfect and whose pane is one flat colour is a
window with no terminal in it, and no check in this repository could tell the
difference before this one.

Five independent records are collected from one launch, and no side can
confirm itself:

  1. the application's renderer, through its own markers -- whether it created
     a composition swap chain and whether Present refused
     (``OpenTerminal Atlas event=...``);
  2. the runtime's SwapChainPanel, through the binding trace -- whether the
     producer's surface arrived, of which kind, and on which layout node
     (``OpenXaml external source=...``);
  3. the renderer's record of the frame it committed -- whether that frame
     actually carries an external surface (``scene-stats external=``), and
     where the owning node was arranged (``scene-node``);
  4. a separate Win32 process's record of the desktop pixels inside the live
     window, on a grid fixed before anything was observed;
  5. the same process's record of where that window is, which is what ties the
     frame's coordinates to the desktop's.

Two boundaries were measured before this gate was written, and each is
reported by name rather than as a silent pass:

  * A composition swap chain can only be presented on the Vulkan wined3d
    renderer. On the OpenGL renderer the producer's Present returns
    E_NOTIMPL (0x80004001) from the shared-resource path every frame and
    AtlasEngine rebuilds the swap chain forever, so there is never anything
    to import. Run with ``WINE_D3D_CONFIG=renderer=vulkan`` and an ICD.
  * Windows Terminal's own OutputDebugStringA output reaches a Wine log on
    the ``seh`` channel, not on ``debugstr``. A launch that traces only
    ``debugstr`` sees the runtime's markers and none of the application's,
    which looks exactly like an application that never rendered. The launch
    below traces both.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))

# The application's own renderer markers. They are plain text inside an
# OutputDebugStringA payload, which a Wine log escapes; matching the payload
# rather than the quoting keeps this independent of how the log was captured.
ATLAS_EVENT = re.compile(r"OpenTerminal Atlas event=([a-z0-9-]+)"
                         r"(?: error=(0x[0-9a-fA-F]{8}))?")
CONTROL_EVENT = re.compile(r"OpenTerminal Control event=([a-z0-9-]+)")
STARTUP_COMPLETE = re.compile(r"OpenTerminal startup event=complete tabs=(\d+)")

# The runtime's own records.
EXTERNAL_BINDING = re.compile(
    r"OpenXaml external source=(\S+) result=(0x[0-9a-fA-F]{8}) "
    r"generation=(\d+) kind=(\d+) bound=(true|false) node_id=(\d+)")
SCENE_STATS = re.compile(r"OpenXaml frame event=scene-stats ([^\"\\\r\n]*)")
SCENE_NODE = re.compile(r"OpenXaml frame event=scene-node ([^\"\\\r\n]*)")
SCENE_EXTERNAL = re.compile(
    r"OpenXaml frame event=scene-external-surface ([^\"\\\r\n]*)")

PROBE_WINDOW = re.compile(r"probe window found=(true|false) client=(\d+)x(\d+) "
                          r"origin=(-?\d+),(-?\d+) attempts=(\d+) observations=(\d+)")
PROBE_SAMPLE = re.compile(r"probe sample index=(\d+) x=(-?\d+) y=(-?\d+) "
                          r"sx=(-?\d+) sy=(-?\d+) rgb=(none|[0-9a-f]{6})")
PROBE_DISPLACEMENT = re.compile(r"probe displacement=([+-]\d+),([+-]\d+)")

LAYERED_CHILD_NOTE = ("research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/"
                      "layered-child-update.md")

# A pane that renders a shell shows at least a background and a prompt. One
# flat colour over the whole pane is the blank pane this gate exists to catch,
# so the pane is required to carry more than one colour rather than merely to
# be non-black: a pane painted entirely in the profile's background colour is
# just as blank as a pane painted entirely in black.
MINIMUM_PANE_SAMPLES = 4


def _field(record: str, name: str) -> str | None:
    match = re.search(rf"(?:^| ){re.escape(name)}=([^ ]+)(?: |$)", record)
    return match.group(1) if match else None


def _numbers(text: str | None, count: int) -> list[float] | None:
    if not text:
        return None
    parts = text.split(",")
    if len(parts) != count:
        return None
    try:
        return [float(part) for part in parts]
    except ValueError:
        return None


def producer_evidence(log: str) -> dict:
    """What the application's renderer says it did."""
    events: list[str] = []
    present_failures: list[str] = []
    for name, error in ATLAS_EVENT.findall(log):
        events.append(name)
        if name == "present-failed" and error:
            present_failures.append(error.lower())
    controls = [name for name in CONTROL_EVENT.findall(log)]
    tabs = STARTUP_COMPLETE.findall(log)
    return {
        "atlas_events": sorted(set(events)),
        "created_composition_swap_chain": "create-composition-swap-chain" in events,
        "entered_present": "present-entered" in events,
        "present_failures": sorted(set(present_failures)),
        "present_failure_count": len(present_failures),
        "control_events": sorted(set(controls)),
        "enabled_painting": "enable-painting-complete" in controls,
        "tabs": int(tabs[-1]) if tabs else None,
    }


def binding_evidence(log: str) -> dict:
    """What the runtime's SwapChainPanel says it was handed."""
    bindings = []
    for source, result, generation, kind, bound, node in EXTERNAL_BINDING.findall(log):
        bindings.append({
            "source": source,
            "result": result.lower(),
            "generation": int(generation),
            "kind": int(kind),
            "bound": bound == "true",
            "node_id": int(node),
        })
    live = [entry for entry in bindings if entry["bound"] and entry["result"] == "0x00000000"]
    return {"bindings": bindings, "live": live,
            "node_id": live[-1]["node_id"] if live else None,
            "source": live[-1]["source"] if live else None}


def _stats_record(body: str) -> dict:
    record: dict = {}
    for name in ("generation", "nodes", "external", "external_imported",
                 "external_reused", "commands"):
        value = _field(body, name)
        record[name] = int(value) if value and value.isdigit() else None
    record["backend"] = _field(body, "backend")
    record["reason"] = _field(body, "reason")
    return record


def frame_evidence(log: str, node_id: int | None) -> dict:
    """The renderer's record of the frames it committed."""
    stats = [_stats_record(body) for body in SCENE_STATS.findall(log)]
    carrying = [record for record in stats
                if record.get("external") and record["external"] > 0]

    # Where the node that owns the producer's surface was arranged. The
    # binding trace names the node; the scene dump places it. Neither record
    # would be enough alone.
    node_rect: list[float] | None = None
    node_generation: int | None = None
    for body in SCENE_NODE.findall(log):
        identifier = _field(body, "id")
        if node_id is None or identifier != str(node_id):
            continue
        origin = _numbers(_field(body, "origin"), 2)
        actual = _numbers(_field(body, "actual"), 2)
        generation = _field(body, "generation")
        if origin is None or actual is None:
            continue
        if actual[0] <= 0.0 or actual[1] <= 0.0:
            continue
        node_rect = [origin[0], origin[1], actual[0], actual[1]]
        node_generation = int(generation) if generation and generation.isdigit() else None

    externals = []
    for body in SCENE_EXTERNAL.findall(log):
        rect = _numbers(_field(body, "rect"), 4)
        if rect is None:
            continue
        externals.append({"rect": rect, "kind": _field(body, "kind"),
                          "path": _field(body, "path") or ""})

    return {
        "committed_frames": len(stats),
        "backends": sorted({record["backend"] for record in stats
                            if record["backend"]}),
        "frames_carrying_external": len(carrying),
        "imported": sum(record["external_imported"] or 0 for record in stats),
        "reused": sum(record["external_reused"] or 0 for record in stats),
        "node_rect": node_rect,
        "node_generation": node_generation,
        "external_records": externals,
    }


def probe_evidence(text: str) -> dict:
    window = PROBE_WINDOW.search(text)
    displacement = PROBE_DISPLACEMENT.search(text)
    samples = []
    for index, x, y, screen_x, screen_y, rgb in PROBE_SAMPLE.findall(text):
        samples.append({"index": int(index), "x": int(x), "y": int(y),
                        "screen": [int(screen_x), int(screen_y)],
                        "rgb": None if rgb == "none" else rgb})
    return {
        "window_found": bool(window) and window.group(1) == "true",
        "client": [int(window.group(2)), int(window.group(3))] if window else None,
        "origin": [int(window.group(4)), int(window.group(5))] if window else None,
        "attempts": int(window.group(6)) if window else 0,
        "observations": int(window.group(7)) if window else 0,
        "displacement": ([int(displacement.group(1)), int(displacement.group(2))]
                         if displacement else None),
        "samples": samples,
    }


def pane_samples(frame: dict, probe: dict) -> list[dict]:
    """The read pixels that lie inside the pane the frame record describes.

    The probe reports each sample in client coordinates; the frame reports the
    pane in the island's coordinates, and the island's client origin is the
    window's client origin. A sample counts only where both records agree it
    is inside the pane.
    """
    rect = frame.get("node_rect")
    if not rect:
        return []
    left, top, width, height = rect
    inside = []
    for sample in probe["samples"]:
        if sample["rgb"] is None:
            continue
        if (left <= sample["x"] < left + width and
                top <= sample["y"] < top + height):
            inside.append(sample)
    return inside


def evaluate(log: str, probe_output: str) -> dict:
    producer = producer_evidence(log)
    binding = binding_evidence(log)
    frame = frame_evidence(log, binding["node_id"])
    probe = probe_evidence(probe_output)
    checks: list[dict] = []

    def record(name: str, passed: bool, detail: str) -> None:
        checks.append({"name": name, "status": "pass" if passed else "fail",
                       "detail": detail})

    def skip(name: str, detail: str) -> None:
        checks.append({"name": name, "status": "skip", "detail": detail})

    record("app-opened-a-tab", producer["tabs"] is not None and producer["tabs"] > 0,
           f"tabs={producer['tabs']} control_events={len(producer['control_events'])}")

    record("app-enabled-painting", producer["enabled_painting"],
           f"control_events={producer['control_events']}")

    record("app-created-composition-swap-chain",
           producer["created_composition_swap_chain"],
           f"atlas_events={producer['atlas_events']}")

    # A refused Present is the producer saying it drew nothing that can be
    # composed. It is a failure of this gate, not a skip: the pane really is
    # blank, and naming the HRESULT says why.
    record("app-present-not-refused",
           producer["entered_present"] and not producer["present_failures"],
           f"entered={producer['entered_present']} "
           f"failures={producer['present_failures']} "
           f"count={producer['present_failure_count']}")

    record("runtime-bound-producer-surface", bool(binding["live"]),
           f"bindings={len(binding['bindings'])} live={len(binding['live'])} "
           f"source={binding['source']} node_id={binding['node_id']}")

    record("committed-frame-carries-producer-surface",
           frame["frames_carrying_external"] > 0,
           f"frames={frame['committed_frames']} backends={frame['backends']} "
           f"carrying={frame['frames_carrying_external']} "
           f"imported={frame['imported']} reused={frame['reused']}")

    record("committed-frame-places-the-pane", frame["node_rect"] is not None,
           f"node_id={binding['node_id']} rect={frame['node_rect']}")

    displacement = probe["displacement"]
    displaced = bool(displacement) and (displacement[0] or displacement[1])
    observed = (probe["window_found"] and probe["observations"] > 0 and
                any(sample["rgb"] for sample in probe["samples"]))
    record("probe-observed-window", observed,
           f"found={probe['window_found']} client={probe['client']} "
           f"observations={probe['observations']} samples={len(probe['samples'])}")

    inside = pane_samples(frame, probe)
    colours = sorted({sample["rgb"] for sample in inside})
    if displaced:
        skip("pane-is-not-blank",
             f"this loader displaces a layered child by "
             f"{displacement[0]:+d},{displacement[1]:+d}, so a pixel read at "
             f"the pane's own coordinates is not the pane; see {LAYERED_CHILD_NOTE}")
    elif len(inside) < MINIMUM_PANE_SAMPLES:
        # Not a skip. With a placed pane and an observed window there is no
        # honest reason for the probe grid to miss it; too few samples means
        # the pane was never placed where the frame says it is.
        record("pane-is-not-blank", False,
               f"only {len(inside)} probe sample(s) landed inside the pane "
               f"rect={frame['node_rect']} (need {MINIMUM_PANE_SAMPLES})")
    else:
        record("pane-is-not-blank", len(colours) > 1,
               f"samples={len(inside)} distinct_colours={colours[:6]} "
               f"rect={frame['node_rect']}")

    failures = [check for check in checks if check["status"] == "fail"]
    return {
        "checks": checks,
        "failed": [check["name"] for check in failures],
        "skipped": [check["name"] for check in checks if check["status"] == "skip"],
        "producer": producer,
        "binding": binding,
        "frame": frame,
        "pane_samples": len(inside),
        "pane_colours": colours,
        "probe": {key: probe[key] for key in
                  ("window_found", "client", "origin", "attempts",
                   "observations", "displacement")},
        "success": not failures,
    }


# A profile the prefix can actually run, and a tab that outlives it.
#
# Both halves are provisioning, not part of what is measured, and both were
# measured before being written down:
#
#   * Terminal's default profile is Windows PowerShell, and a Wine prefix's
#     System32\WindowsPowerShell\v1.0 directory is empty. The profile cannot
#     start, so the session ends at once.
#   * With the default closeOnExit, a session that ends closes its tab, the
#     last tab closes the window and the process exits -- measured at 0.8-0.9
#     seconds after startup, which is before a swap chain has been presented
#     more than once and long before any pixel can be read. `never` keeps the
#     pane on the screen after its shell is gone, which is exactly the state
#     this gate needs to photograph.
#
# Deliberately absent: a font face. A face the prefix cannot resolve makes
# AtlasEngine compute a zero cell size and the process dies with
# EXCEPTION_INT_DIVIDE_BY_ZERO, so the fonts installed by
# install_deployment_fonts are left to speak for themselves.
PROVISIONED_SETTINGS = """{
    "$schema": "https://aka.ms/terminal-profiles-schema",
    "defaultProfile": "{0caa0dad-35be-5f56-a8ff-afceeeaa6101}",
    "profiles":
    {
        "defaults": { "closeOnExit": "never" },
        "list":
        [
            {
                "guid": "{0caa0dad-35be-5f56-a8ff-afceeeaa6101}",
                "name": "Command Prompt",
                "commandline": "cmd.exe",
                "hidden": false
            }
        ]
    },
    "schemes": [],
    "actions": []
}
"""


def provision_terminal_settings(prefix: Path) -> list[str]:
    """Give the prefix a profile it can run and a tab that outlives it."""
    written: list[str] = []
    users = prefix / "drive_c" / "users"
    if not users.is_dir():
        return written
    for user in sorted(users.iterdir()):
        if not user.is_dir() or user.name == "Public":
            continue
        folder = user / "AppData" / "Local" / "Microsoft" / "Windows Terminal"
        folder.mkdir(parents=True, exist_ok=True)
        target = folder / "settings.json"
        target.write_text(PROVISIONED_SETTINGS, encoding="utf-8", newline="\n")
        written.append(str(target))
    return written


def launch(dll: Path, executable: Path, xbf_root: Path, prefix: Path,
           probe_executable: Path, timeout_seconds: int, log_path: Path,
           probe_log_path: Path, wine: str, runtime_font_spec: Path,
           runtime_font_cache: Path, columns: int, rows: int) -> tuple[str, str, dict]:
    import run_terminal_integration as integration
    import check_xbf_ui_render as xbf_gate
    from build_xamlcore import registration
    from install_deployment_fonts import install as install_deployment_fonts
    from prepare_runtime_fonts import prepare as prepare_runtime_fonts

    integration.require_tool("timeout")
    runtime = integration.find_mingw_runtime()

    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEARCH"] = "win64"
    # `seh` carries the application's OutputDebugStringA payloads in this
    # loader and `debugstr` carries the runtime's; tracing one of the two
    # hides half of the evidence this gate compares.
    environment["WINEDEBUG"] = "err+all,warn+debugstr,warn+seh,fixme+combase"
    environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    environment["WINEPATH"] = integration.wine_search_path(runtime)
    environment["OPENXAML_XBF_ROOT"] = integration.wine_path(xbf_root)
    environment["OPENXAML_TRACE_FRAMES"] = "1"
    environment["OPENXAML_TRACE_SCENE"] = "1"
    environment["OPENXAML_TRACE_VISUAL_TREE"] = "1"
    font_alias_manifest = prepare_runtime_fonts(
        runtime_font_spec, runtime_font_cache, "wine")
    environment["OPENXAML_FONT_ALIAS_MANIFEST"] = integration.wine_path(
        font_alias_manifest)

    subprocess.run([wine, "wineboot.exe", "-u"], env=environment, check=True)
    registry_file = prefix / "openxaml-pane-render.reg"
    registry_file.write_text(registration(dll), encoding="utf-8", newline="\n")
    subprocess.run([wine, "regedit.exe", str(registry_file)], env=environment,
                   check=True)
    provisioned = xbf_gate.provision_environment_registry(prefix, environment, wine)
    fonts = install_deployment_fonts(executable.parent, prefix, environment, wine)
    settings = provision_terminal_settings(prefix)

    probe_process = subprocess.Popen(
        [wine, str(probe_executable), "--deadline-ms",
         str(max(1000, timeout_seconds * 1000 - 2000)),
         "--columns", str(columns), "--rows", str(rows)],
        env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace")
    terminal = subprocess.run(
        ["timeout", "--signal=TERM", "--kill-after=5", str(timeout_seconds),
         wine, "./" + executable.name],
        env=environment, cwd=executable.parent, capture_output=True, text=True,
        errors="replace", check=False)
    probe_output, _ = probe_process.communicate(timeout=60)

    log = terminal.stderr + terminal.stdout
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(log, encoding="utf-8", errors="replace", newline="\n")
    probe_log_path.write_text(probe_output, encoding="utf-8", errors="replace",
                              newline="\n")
    return log, probe_output, {
        "deployment_fonts": [font["file"] for font in fonts["fonts"]],
        "provisioned_registry_values": provisioned,
        "provisioned_settings": settings,
        "terminal_exit_code": terminal.returncode,
        "wine_loader": wine,
        "wine_d3d_config": environment.get("WINE_D3D_CONFIG"),
        "vk_driver_files": environment.get("VK_DRIVER_FILES"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xaml-dll", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path,
                        help="the built terminal_pixel_probe.exe")
    parser.add_argument("--xbf-root", type=Path)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--wine-loader", type=Path)
    parser.add_argument("--wineserver", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--probe-log", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--columns", type=int, default=24)
    parser.add_argument("--rows", type=int, default=24)
    parser.add_argument("--runtime-font-spec", type=Path)
    parser.add_argument("--runtime-font-cache", type=Path,
                        default=Path(tempfile.gettempdir()) /
                                "openterminal-runtime-fonts")
    parser.add_argument("--no-xvfb", action="store_true")
    arguments = parser.parse_args()
    if arguments.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    if not arguments.no_xvfb and not os.environ.get("DISPLAY"):
        xvfb_run = _require_tool("xvfb-run")
        command = [xvfb_run, "--auto-servernum", sys.executable, "-B",
                   str(Path(__file__).resolve()), *sys.argv[1:], "--no-xvfb"]
        return subprocess.run(command, check=False).returncode

    import run_terminal_integration as integration
    from prepare_runtime_fonts import DEFAULT_SPEC as DEFAULT_RUNTIME_FONT_SPEC

    dll = integration.require_file(arguments.xaml_dll, "OpenXaml DLL")
    executable = integration.require_file(arguments.executable,
                                          "Terminal executable")
    probe = integration.require_file(arguments.probe, "pixel probe")
    xbf_root = (arguments.xbf_root or executable.parent).resolve()
    if not xbf_root.is_dir():
        raise SystemExit(f"XBF root does not exist: {xbf_root}")
    runtime_font_spec = integration.require_file(
        arguments.runtime_font_spec or DEFAULT_RUNTIME_FONT_SPEC,
        "runtime font specification")
    wine = (str(integration.require_file(arguments.wine_loader, "Wine loader"))
            if arguments.wine_loader else integration.require_tool("wine"))
    wineserver = (str(integration.require_file(arguments.wineserver, "wineserver"))
                  if arguments.wineserver else integration.require_tool("wineserver"))

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if arguments.prefix:
        prefix = integration.ensure_fresh_prefix(arguments.prefix)
    else:
        temporary = tempfile.TemporaryDirectory(prefix="openterminal-pane-")
        prefix = Path(temporary.name).resolve()
    log_path = (arguments.log.resolve() if arguments.log
                else prefix / "pane-render.log")
    probe_log_path = (arguments.probe_log.resolve() if arguments.probe_log
                      else prefix / "pane-render-probe.log")

    started = time.time()
    try:
        log, probe_output, launch_facts = launch(
            dll, executable, xbf_root, prefix, probe, arguments.timeout,
            log_path, probe_log_path, wine, runtime_font_spec,
            arguments.runtime_font_cache.resolve(), arguments.columns,
            arguments.rows)
        result = evaluate(log, probe_output)
        result.update(launch_facts)
        result.update({
            "elapsed_seconds": round(time.time() - started, 3),
            "executable": str(executable),
            "executable_sha256": integration.sha256(executable),
            "log": str(log_path),
            "openxaml_dll": str(dll),
            "openxaml_dll_sha256": integration.sha256(dll),
            "prefix": str(prefix),
            "probe_log": str(probe_log_path),
        })
        text = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if arguments.report:
            arguments.report.write_text(text, encoding="utf-8", newline="\n")
        sys.stdout.write(text)
        for check in result["checks"]:
            if check["status"] != "pass":
                print(f"{check['status'].upper()}: {check['name']} -- "
                      f"{check['detail']}", file=sys.stderr)
        return 0 if result["success"] else 1
    finally:
        subprocess.run([wineserver, "-k"],
                       env={**os.environ, "WINEPREFIX": str(prefix)}, check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if temporary:
            temporary.cleanup()


def _require_tool(name: str) -> str:
    import shutil
    located = shutil.which(name)
    if not located:
        raise SystemExit(f"required tool is not on PATH: {name}")
    return located


if __name__ == "__main__":
    raise SystemExit(main())
