#!/usr/bin/env python3
"""Cross-compiles the GDI render backend and runs it under Wine.

The render core is plain C++17 and is built, tested and gated on Linux by
`phase3/render/CMakeLists.txt`. This script builds the other half: the backend
that puts glyphs on a surface, which needs a Windows API, and the tiny host that
puts a surface in a window, which needs a window.

Everything lands under one scratch root and nothing is committed. Modelled on
build_xamlcore.py, and deliberately given a different default root so the two
never share a directory.

    python3 phase3/scripts/build_render.py --cases phase3/xaml-db/cases \\
        --fonts <a fonts directory>

Needs `x86_64-w64-mingw32-g++` and `wine`. The live-window step also needs an X
display; with none it is skipped by name rather than passed.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

PHASE3_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = PHASE3_DIR.parent

# The same list phase3/layout/CMakeLists.txt and build_xamlcore.py carry, and
# for the same reason build_xamlcore.py states: a source missing from here is a
# link failure naming the symbol, which is the failure mode worth having. Not
# imported from build_xamlcore because that module reaches into phase2's mingw
# helpers, and this script has no use for a Windows SDK payload.
LAYOUT_SOURCES = ["property.cpp", "element.cpp", "events.cpp", "border.cpp", "control.cpp",
                  "stack_panel.cpp", "grid.cpp", "chrome.cpp", "canvas.cpp",
                  "content_presenter.cpp", "geometry.cpp", "image.cpp", "shape.cpp",
                  "icon.cpp", "brush.cpp", "text.cpp", "fonts.cpp", "json.cpp",
                  "scroll_viewer.cpp", "basic_controls.cpp",
                  "resources.cpp", "style.cpp", "xdirectives.cpp", "resw_strings.cpp",
                  "binding.cpp", "visual_state.cpp", "default_styles.cpp",
                  "advanced_controls.cpp"]

RENDER_SOURCES = ["display_list.cpp", "surface.cpp", "case_runner.cpp"]

DEFAULT_ROOT = Path("/tmp/openterminal-render")


def run(arguments: list[str], env: dict[str, str] | None = None, check: bool = True):
    printable = " ".join(str(a) for a in arguments)
    print(f"$ {printable}", flush=True)
    return subprocess.run(arguments, check=check, env=env)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT,
                        help=f"scratch root (default: {DEFAULT_ROOT})")
    parser.add_argument("--cases", type=Path,
                        default=PHASE3_DIR / "xaml-db" / "cases")
    parser.add_argument("--fonts", type=Path,
                        default=PHASE3_DIR / "xaml-db" / "fonts")
    parser.add_argument("--theme-resources", type=Path,
                        default=PHASE3_DIR / "xaml-db" / "theme-resources")
    parser.add_argument("--prefix", type=Path, help="Wine prefix (default: <root>/wine-prefix)")
    parser.add_argument("--window-case", type=str, default=None,
                        help="a case id to paint in a live window")
    parser.add_argument("--ink-font", type=Path, default=None,
                        help="a font file to load privately and paint ink samples in; the "
                             "one font whose metrics and glyphs can both be had here")
    parser.add_argument("--ink-family", type=str, default="Cascadia Mono",
                        help="the family name inside --ink-font")
    parser.add_argument("--skip-run", action="store_true",
                        help="build only")
    args = parser.parse_args()

    for tool in ("x86_64-w64-mingw32-g++", "wine"):
        if shutil.which(tool) is None:
            print(f"::error::{tool} is not on PATH")
            return 3

    root: Path = args.root
    root.mkdir(parents=True, exist_ok=True)
    prefix = args.prefix or root / "wine-prefix"

    layout_src = PHASE3_DIR / "layout" / "src"
    render_src = PHASE3_DIR / "render" / "src"
    gdi_src = PHASE3_DIR / "render" / "gdi"
    includes = ["-I" + str(layout_src), "-I" + str(render_src), "-I" + str(gdi_src)]

    # Static everything, for the reason build_xamlcore.py gives: a binary that
    # pulls in libstdc++-6.dll fails under Wine with a bare MOD_NOT_FOUND.
    common = ["x86_64-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall", "-Wextra",
              "-static", "-static-libgcc", "-static-libstdc++"]
    libraries = ["-lgdi32", "-luser32"]

    sources = ([str(layout_src / name) for name in LAYOUT_SOURCES]
               + [str(layout_src / "markup.cpp")]
               + [str(render_src / name) for name in RENDER_SOURCES]
               + [str(gdi_src / "gdi_target.cpp")])

    harness = root / "render_cases_gdi.exe"
    run(common + ["-o", str(harness), str(gdi_src / "render_cases_gdi.cpp")]
        + sources + includes + libraries)

    host = root / "xaml_window.exe"
    run(common + ["-o", str(host), str(gdi_src / "xaml_window.cpp")]
        + sources + includes + libraries)

    ink = root / "ink_check.exe"
    run(common + ["-o", str(ink), str(gdi_src / "ink_check.cpp")]
        + sources + includes + libraries)

    if args.skip_run:
        print(f"built {harness}, {host} and {ink}")
        return 0

    environment = os.environ.copy()
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEDEBUG"] = environment.get("WINEDEBUG", "-all")
    if not (prefix / "system.reg").is_file():
        run(["wineboot", "-u"], env=environment)

    results = root / "gdi-dumps"
    if results.exists():
        shutil.rmtree(results)
    run(["wine", str(harness), str(args.cases.resolve()), str(results),
         str(args.fonts.resolve()), str(args.theme_resources.resolve())], env=environment)
    print(f"dumps in {results}")

    if args.ink_font:
        ink_dumps = root / "ink-dumps"
        if ink_dumps.exists():
            shutil.rmtree(ink_dumps)
        completed = run(["wine", str(ink), str(args.ink_font.resolve()), args.ink_family,
                         str(args.fonts.resolve()), str(ink_dumps)],
                        env=environment, check=False)
        if completed.returncode != 0:
            print("::error::the ink samples did not paint")
            return 5
        print(f"ink dumps in {ink_dumps}")

    if args.window_case:
        case_file = None
        for candidate in args.cases.rglob(f"{args.window_case}.json"):
            case_file = candidate
            break
        if case_file is None:
            print(f"::error::no case named {args.window_case} under {args.cases}")
            return 4
        window_dump = root / f"{args.window_case}.window.ppm"
        if not environment.get("DISPLAY"):
            print(f"::notice::no DISPLAY; the live-window check for {args.window_case} is "
                  "skipped by name rather than passed")
            return 0
        # Wine's first window on a fresh prefix can lose a race with the
        # desktop; one retry is enough and a second failure is a real one.
        for attempt in range(2):
            completed = run(["wine", str(host), str(case_file), str(window_dump),
                             str(args.fonts.resolve()), str(args.theme_resources.resolve())],
                            env=environment, check=False)
            if completed.returncode == 0:
                break
            time.sleep(2)
        else:
            print("::error::the live window never painted")
            return 5
        print(f"window read-back in {window_dump}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
