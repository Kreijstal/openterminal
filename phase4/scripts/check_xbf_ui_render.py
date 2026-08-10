#!/usr/bin/env python3
"""Prove that the UI Terminal's own XBF pages define is what gets rendered.

The boot gate already answers "does the process live and commit a frame".
That is not the same question as "is the application's own markup on screen":
a frame committed from an empty tree, or committed and never presented, passes
the boot gate and shows nothing. This gate closes that gap end to end, and it
does it without an oracle -- every expectation below is derived, in the same
run, from either Terminal's own compiled markup or the runtime's own record of
the frame it committed.

Three independent records are collected from one launch:

  1. the XBF loader's record of the pages it materialized -- each page's root
     type and the element types its object graph declares;
  2. the retained renderer's record of the frame it committed -- the scene
     nodes with their types, paths and geometry, and the fill commands with
     their rectangles and colours;
  3. a separate Win32 process's record of the desktop pixels inside the live
     hosting window, sampled on a grid fixed before anything was observed.

The checks tie those together: the types the XBF declares must appear in the
scene the renderer committed, the scene must actually paint, and the pixel a
sample landed on must be the colour the renderer's own fill record says covers
that point. Neither side can confirm itself.

The pixel checks -- and only those -- are conditional on the loader placing a
layered child where UpdateLayeredWindow was told to put it. The probe measures
that with plain GDI before it reads anything, and a loader with the defect
recorded in research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/
layered-child-update.md gets a named skip rather than a silent pass.
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

LAYERED_CHILD_NOTE = ("research/wine/af5241854c513c2e68938425cc6cd3cac40b943a/"
                      "layered-child-update.md")

# Wine renders one OutputDebugString call as an escaped C string, so the
# quotes the runtime writes arrive as \" in a captured log and unescaped when
# the record is read directly. Accept both rather than depending on the capture.
XBF_LOADED = re.compile(
    r'OpenXaml xbf event=loaded page=\\?"([^"\\]*)\\?" root=(\S+) objects=(\d+)')
XBF_TYPE = re.compile(
    r'OpenXaml xbf event=type page=\\?"([^"\\]*)\\?" name=(\S+) count=(\d+)')
SCENE_NODE = re.compile(r"OpenXaml frame event=scene-node ([^\"\\\r\n]*)")
SCENE_FILL = re.compile(r"OpenXaml frame event=scene-fill ([^\"\\\r\n]*)")
SCENE_TEXT = re.compile(r"OpenXaml frame event=scene-text ([^\"\\\r\n]*)")
PRESENT_ORIGIN = re.compile(r"OpenXaml frame event=present-origin ([^\"\\\r\n]*)")

# A loader that aborts the process at a function it does not implement never
# lets the window appear. That is a named property of the loader, not of the
# runtime under test, and it is reported as a named skip rather than as either
# a pass or a failure of the renderer.
LOADER_GAP = re.compile(r"[Uu]nimplemented function (\S+) called")

PROBE_DISPLACEMENT = re.compile(r"probe displacement=([+-]\d+),([+-]\d+)")
PROBE_WINDOW = re.compile(r"probe window found=(true|false) client=(\d+)x(\d+) "
                          r"origin=(-?\d+),(-?\d+) attempts=(\d+) observations=(\d+)")
PROBE_SAMPLE = re.compile(r"probe sample index=(\d+) x=(-?\d+) y=(-?\d+) "
                          r"sx=(-?\d+) sy=(-?\d+) rgb=(none|[0-9a-f]{6})")

# A compiled page whose root is the Application object is the application's
# resource dictionary, not a view: it has no place in any visual tree, and
# requiring it in the rendered scene would be requiring something that cannot
# be true. Every other page root is a UI element and must appear.
NON_VISUAL_PAGE_ROOTS = frozenset({"Windows.UI.Xaml.Application"})

# A fill has to be worth reading a pixel out of. One cell of the probe grid is
# about a hundredth of the client area; this is comfortably larger than that,
# so a sample that lands inside the chosen fill is not landing on its edge.
MINIMUM_FILL_AREA = 4096.0


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


def xbf_evidence(log: str) -> dict:
    """What the XBF loader says it materialized, per page."""
    pages: dict[str, dict] = {}
    for page, root, objects in XBF_LOADED.findall(log):
        pages[page] = {"root": root, "objects": int(objects), "types": {}}
    for page, name, count in XBF_TYPE.findall(log):
        entry = pages.setdefault(page, {"root": None, "objects": 0, "types": {}})
        entry["types"][name] = int(count)
    declared: set[str] = set()
    for entry in pages.values():
        declared.update(entry["types"])
        if entry["root"]:
            declared.add(entry["root"])
    return {
        "pages": pages,
        "declared_types": sorted(declared),
        "roots": sorted({entry["root"] for entry in pages.values() if entry["root"]}),
    }


def _scene_records(log: str, pattern: re.Pattern[str]) -> dict[int, list[dict]]:
    """Group one kind of scene record by the generation it describes."""
    grouped: dict[int, list[dict]] = {}
    for body in pattern.findall(log):
        generation = _field(body, "generation")
        if generation is None or not generation.isdigit():
            continue
        grouped.setdefault(int(generation), []).append(body)
    return grouped


def scene_evidence(log: str) -> dict:
    """The renderer's own record of the newest frame it dumped."""
    nodes_by_generation = _scene_records(log, SCENE_NODE)
    fills_by_generation = _scene_records(log, SCENE_FILL)
    texts_by_generation = _scene_records(log, SCENE_TEXT)
    if not nodes_by_generation:
        return {"generation": None, "nodes": [], "fills": [], "texts": []}
    generation = max(nodes_by_generation)

    nodes = []
    for body in nodes_by_generation[generation]:
        slot = _numbers(_field(body, "slot"), 4)
        actual = _numbers(_field(body, "actual"), 2)
        origin = _numbers(_field(body, "origin"), 2)
        nodes.append({
            "index": int(_field(body, "index") or -1),
            "type": _field(body, "type") or "",
            "visible": _field(body, "visible") == "true",
            "slot": slot,
            "actual": actual,
            "origin": origin,
            "path": _field(body, "path") or "",
        })

    fills = []
    for body in fills_by_generation.get(generation, []):
        rect = _numbers(_field(body, "rect"), 4)
        color = _field(body, "color") or ""
        if rect is None or len(color) != 8:
            continue
        fills.append({
            "index": int(_field(body, "index") or -1),
            "what": _field(body, "what") or "",
            "rect": rect,
            "alpha": int(color[0:2], 16),
            "rgb": color[2:8],
            "path": _field(body, "path") or "",
        })

    texts = []
    for body in texts_by_generation.get(generation, []):
        rect = _numbers(_field(body, "rect"), 4)
        if rect is not None:
            texts.append({"rect": rect, "path": _field(body, "path") or ""})

    return {
        "generation": generation,
        "nodes": nodes,
        "fills": sorted(fills, key=lambda fill: fill["index"]),
        "texts": texts,
        "types": sorted({node["type"] for node in nodes if node["type"]}),
    }


def present_evidence(log: str) -> dict:
    """Where the newest presented frame put its pixels on the desktop."""
    best: dict = {"generation": None, "screen": None, "extent": None}
    for body in PRESENT_ORIGIN.findall(log):
        generation = _field(body, "generation")
        screen = _numbers(_field(body, "screen"), 2)
        extent = _field(body, "extent")
        if generation is None or not generation.isdigit() or screen is None:
            continue
        candidate = int(generation)
        if best["generation"] is None or candidate >= best["generation"]:
            size = re.match(r"^(\d+)x(\d+)$", extent or "")
            best = {
                "generation": candidate,
                "screen": [int(screen[0]), int(screen[1])],
                "extent": [int(size.group(1)), int(size.group(2))] if size else None,
            }
    return best


def probe_evidence(text: str) -> dict:
    displacement = PROBE_DISPLACEMENT.search(text)
    window = PROBE_WINDOW.search(text)
    samples = []
    for index, x, y, screen_x, screen_y, rgb in PROBE_SAMPLE.findall(text):
        samples.append({
            "index": int(index),
            "x": int(x),
            "y": int(y),
            "screen": [int(screen_x), int(screen_y)],
            "rgb": None if rgb == "none" else rgb,
        })
    return {
        "displacement": ([int(displacement.group(1)), int(displacement.group(2))]
                         if displacement else None),
        "window_found": bool(window) and window.group(1) == "true",
        "client": ([int(window.group(2)), int(window.group(3))] if window else None),
        "origin": ([int(window.group(4)), int(window.group(5))] if window else None),
        "attempts": int(window.group(6)) if window else 0,
        "observations": int(window.group(7)) if window else 0,
        "samples": samples,
    }


def _covers(rect: list[float], x: float, y: float) -> bool:
    return (rect[0] <= x < rect[0] + rect[2] and
            rect[1] <= y < rect[1] + rect[3])


def pixel_expectation(scene: dict, present: dict, probe: dict) -> dict | None:
    """The one sample whose colour the renderer's own record predicts.

    A sample qualifies only where the prediction is unambiguous: the point must
    lie inside a fully opaque fill that no later fill, and no text run, can
    have touched. Among those, the largest such fill is chosen, so the reading
    is taken well inside a region rather than near an edge.
    """
    if not scene["fills"] or present.get("screen") is None:
        return None
    origin_x, origin_y = present["screen"]
    candidates = []
    for sample in probe["samples"]:
        if sample["rgb"] is None:
            continue
        # The probe reads the desktop; the renderer records the frame. The
        # present record is what ties the two coordinate spaces together.
        x = sample["screen"][0] - origin_x
        y = sample["screen"][1] - origin_y
        covering = [fill for fill in scene["fills"] if _covers(fill["rect"], x, y)]
        if not covering:
            continue
        topmost = covering[-1]
        if topmost["alpha"] != 255:
            continue
        if any(_covers(text["rect"], x, y) for text in scene["texts"]):
            continue
        area = topmost["rect"][2] * topmost["rect"][3]
        if area < MINIMUM_FILL_AREA:
            continue
        candidates.append((area, -sample["index"], sample, topmost))
    if not candidates:
        return None
    _, _, sample, fill = max(candidates, key=lambda item: (item[0], item[1]))
    return {"sample": sample, "fill": fill}


def evaluate(log: str, probe_output: str, minimum_nodes: int,
             minimum_shared_types: int) -> dict:
    xbf = xbf_evidence(log)
    scene = scene_evidence(log)
    present = present_evidence(log)
    probe = probe_evidence(probe_output)
    checks: list[dict] = []

    def record(name: str, passed: bool, detail: str) -> None:
        checks.append({"name": name, "status": "pass" if passed else "fail",
                       "detail": detail})

    def skip(name: str, detail: str) -> None:
        checks.append({"name": name, "status": "skip", "detail": detail})

    record("xbf-pages-materialized",
           bool(xbf["pages"]) and all(entry["objects"] > 0
                                      for entry in xbf["pages"].values()),
           f"pages={sorted(xbf['pages'])} roots={xbf['roots']}")

    scene_types = set(scene.get("types", []))
    record("scene-dump-present", len(scene["nodes"]) >= minimum_nodes,
           f"generation={scene['generation']} nodes={len(scene['nodes'])} "
           f"required>={minimum_nodes}")

    visual_roots = [root for root in xbf["roots"]
                    if root not in NON_VISUAL_PAGE_ROOTS]
    missing_roots = [root for root in visual_roots if root not in scene_types]
    record("xbf-page-roots-in-scene",
           bool(visual_roots) and not missing_roots,
           f"roots={visual_roots} missing={missing_roots} "
           f"not_visual={sorted(set(xbf['roots']) & NON_VISUAL_PAGE_ROOTS)}")

    shared = sorted(scene_types & set(xbf["declared_types"]))
    record("scene-types-declared-by-xbf",
           len(shared) >= minimum_shared_types,
           f"shared={len(shared)} required>={minimum_shared_types} "
           f"examples={shared[:8]}")

    opaque = [fill for fill in scene["fills"]
              if fill["alpha"] == 255 and
              fill["rect"][2] * fill["rect"][3] >= MINIMUM_FILL_AREA]
    record("scene-paints-opaque-fill", bool(opaque),
           f"fills={len(scene['fills'])} opaque_large={len(opaque)}")

    record("frame-presented",
           present["generation"] is not None and
           present["generation"] == scene["generation"],
           f"presented_generation={present['generation']} "
           f"scene_generation={scene['generation']} screen={present['screen']}")

    observed = (probe["window_found"] and probe["observations"] > 0 and
                any(sample["rgb"] for sample in probe["samples"]))
    # A loader that aborts the process at a function it has not implemented
    # takes the window away before it can be shown. Name it, and read nothing
    # about the renderer into it either way.
    loader_gaps = sorted(set(LOADER_GAP.findall(log)))
    aborted_by_loader = bool(loader_gaps) and not observed
    displacement = probe["displacement"]
    displaced = bool(displacement) and (displacement[0] or displacement[1])
    # Two independent reasons a pixel cannot be read, each named for what it
    # actually is. The window one is not a property of the pixels at all.
    aborted_reason = ("this loader aborted the process at unimplemented "
                      f"function(s) {', '.join(loader_gaps)} before the window "
                      "was shown")
    displaced_reason = (f"this loader displaces a layered child by "
                        f"{displacement[0]:+d},{displacement[1]:+d}; see "
                        f"{LAYERED_CHILD_NOTE}") if displaced else ""
    pixel_skip: str | None = None
    if aborted_by_loader:
        pixel_skip = aborted_reason
    elif displaced:
        pixel_skip = displaced_reason

    if aborted_by_loader:
        skip("probe-observed-window",
             f"no window to read: {aborted_reason} "
             f"(attempts={probe['attempts']})")
    else:
        record("probe-observed-window", observed,
               f"found={probe['window_found']} client={probe['client']} "
               f"observations={probe['observations']} "
               f"samples={len(probe['samples'])}")

    expectation = pixel_expectation(scene, present, probe)
    if pixel_skip:
        skip("pixel-matches-committed-fill", pixel_skip)
        skip("pixel-is-not-blank", pixel_skip)
    elif expectation is None:
        # Not a skip: with a placed layered child there is no honest reason for
        # the renderer's record and the probe's grid to share no point.
        record("pixel-matches-committed-fill", False,
               "no probe sample lies inside an unambiguous opaque fill of the "
               "committed frame")
        record("pixel-is-not-blank", False, "no comparable sample")
    else:
        sample = expectation["sample"]
        fill = expectation["fill"]
        record("pixel-matches-committed-fill", sample["rgb"] == fill["rgb"],
               f"sample={sample['screen']} read={sample['rgb']} "
               f"expected={fill['rgb']} fill={fill['what']} path={fill['path']}")
        record("pixel-is-not-blank",
               any(other["rgb"] not in (None, "000000")
                   for other in probe["samples"]),
               "at least one sampled pixel is not the empty desktop")

    failures = [check for check in checks if check["status"] == "fail"]
    return {
        "checks": checks,
        "failed": [check["name"] for check in failures],
        "skipped": [check["name"] for check in checks
                    if check["status"] == "skip"],
        "layered_child_displacement": displacement,
        "loader_gaps": loader_gaps,
        "present": present,
        "probe": {key: probe[key] for key in
                  ("displacement", "window_found", "client", "origin",
                   "attempts", "observations")},
        "scene": {"generation": scene["generation"],
                  "nodes": len(scene["nodes"]),
                  "fills": len(scene["fills"]),
                  "types": scene.get("types", [])},
        "xbf": {"pages": {page: {"root": entry["root"],
                                 "objects": entry["objects"],
                                 "types": len(entry["types"])}
                          for page, entry in xbf["pages"].items()}},
        "success": not failures,
    }


def provision_environment_registry(prefix: Path, environment: dict[str, str],
                                   wine: str) -> list[str]:
    """Values a fresh Wine prefix lacks and Terminal's environment block needs.

    ``til::env::regenerate`` reads every value in the 64-bit program-files map
    and throws on the first one that is missing, so a prefix without
    ``ProgramW6432Dir`` fails ConPTY initialization with ERROR_FILE_NOT_FOUND
    before a tab exists. On Windows x64 both values are always present. This is
    environment provisioning, the same kind as installing the fonts Terminal
    deploys -- it is not part of what is being measured.
    """
    values = [("ProgramW6432Dir", r"C:\Program Files"),
              ("CommonW6432Dir", r"C:\Program Files\Common Files")]
    provisioned = []
    for name, data in values:
        completed = subprocess.run(
            [wine, "reg.exe", "add",
             r"HKLM\Software\Microsoft\Windows\CurrentVersion",
             "/v", name, "/t", "REG_SZ", "/d", data, "/f"],
            env=environment, capture_output=True, text=True, check=False,
            errors="replace")
        if completed.returncode != 0:
            raise SystemExit(f"could not provision {name} in {prefix}: "
                             f"{completed.stdout}{completed.stderr}")
        provisioned.append(name)
    return provisioned


def launch(dll: Path, executable: Path, xbf_root: Path, prefix: Path,
           probe_executable: Path, timeout_seconds: int, log_path: Path,
           probe_log_path: Path, wine: str, runtime_font_spec: Path,
           runtime_font_cache: Path) -> tuple[str, str, dict]:
    import run_terminal_integration as integration
    from build_xamlcore import registration
    from install_deployment_fonts import install as install_deployment_fonts
    from prepare_runtime_fonts import prepare as prepare_runtime_fonts

    integration.require_tool("timeout")
    runtime = integration.find_mingw_runtime()

    environment = dict(os.environ)
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEARCH"] = "win64"
    environment["WINEDEBUG"] = "err+all,warn+debugstr,fixme+combase"
    environment["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    environment["WINEPATH"] = integration.wine_path(runtime)
    environment["OPENXAML_XBF_ROOT"] = integration.wine_path(xbf_root)
    environment["OPENXAML_TRACE_FRAMES"] = "1"
    environment["OPENXAML_TRACE_SCENE"] = "1"
    environment["OPENXAML_TRACE_XBF"] = "1"
    font_alias_manifest = prepare_runtime_fonts(
        runtime_font_spec, runtime_font_cache, "wine")
    environment["OPENXAML_FONT_ALIAS_MANIFEST"] = integration.wine_path(
        font_alias_manifest)

    subprocess.run([wine, "wineboot.exe", "-u"], env=environment, check=True)
    registry_file = prefix / "openxaml-xbf-ui.reg"
    registry_file.write_text(registration(dll), encoding="utf-8", newline="\n")
    subprocess.run([wine, "regedit.exe", str(registry_file)], env=environment,
                   check=True)
    provisioned = provision_environment_registry(prefix, environment, wine)
    fonts = install_deployment_fonts(executable.parent, prefix, environment, wine)

    # The probe starts first and outlives the launch: it has to be watching
    # before the window exists, and its grid is fixed before it sees anything.
    probe_process = subprocess.Popen(
        [wine, str(probe_executable), "--deadline-ms",
         str(max(1000, timeout_seconds * 1000 - 2000))],
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
        "terminal_exit_code": terminal.returncode,
        "wine_loader": wine,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xaml-dll", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path,
                        help="the built terminal_pixel_probe.exe")
    parser.add_argument("--xbf-root", type=Path)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--wine-loader", type=Path)
    parser.add_argument("--wineserver", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--probe-log", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--minimum-nodes", type=int, default=24)
    parser.add_argument("--minimum-shared-types", type=int, default=6)
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
        temporary = tempfile.TemporaryDirectory(prefix="openterminal-xbf-ui-")
        prefix = Path(temporary.name).resolve()
    log_path = (arguments.log.resolve() if arguments.log
                else prefix / "xbf-ui-render.log")
    probe_log_path = (arguments.probe_log.resolve() if arguments.probe_log
                      else prefix / "xbf-ui-probe.log")

    started = time.time()
    try:
        log, probe_output, launch_facts = launch(
            dll, executable, xbf_root, prefix, probe, arguments.timeout,
            log_path, probe_log_path, wine, runtime_font_spec,
            arguments.runtime_font_cache.resolve())
        result = evaluate(log, probe_output, arguments.minimum_nodes,
                          arguments.minimum_shared_types)
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
            "xbf_root": str(xbf_root),
        })
        text = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if arguments.report:
            arguments.report.write_text(text, encoding="utf-8", newline="\n")
        sys.stdout.write(text)
        for check in result["checks"]:
            if check["status"] != "pass":
                print(f"{check['status'].upper()}: {check['name']} -- "
                      f"{check['detail']}", file=sys.stderr)
        if result["skipped"]:
            print(f"{len(result['skipped'])} check(s) skipped by name on this "
                  "loader; everything else was enforced.", file=sys.stderr)
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
