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
        --fonts "$(python3 phase3/scripts/fetch_measurements.py --fonts)" \\
        --theme-resources <the extracted WinUI dictionary>

Needs `x86_64-w64-mingw32-g++` and `wine`. Two steps also need an X display and
say so by name rather than passing without one: the live window, and the island
frame cache test, whose layered-child case creates real windows.

`--fonts` is not optional in practice. `phase3/xaml-db/fonts` holds only the two
numbers the corpus solved for itself (`derived/`); the harvested per-family
metrics are a CI artifact, and without them every text and icon case loads with
`no harvested metrics for the font family ...` and lands in the checker's "not
laid out" column instead of being measured. The same is true of
`--theme-resources`: it is `extract_winui_theme_resources.py` output and is not
committed, and without it the level 5 and level 7 cases that name a WinUI
resource key fail to load. Both directories' identities are recorded in the
dump root's provenance record -- see render_provenance.py.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import render_provenance  # noqa: E402
from prepare_runtime_fonts import (  # noqa: E402
    DEFAULT_SPEC as RUNTIME_FONT_SPEC,
    prepare as prepare_runtime_fonts,
    windows_path,
)

PHASE3_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = PHASE3_DIR.parent

# The sidecar shape render_cases writes; kept in step with the constant of the
# same meaning in phase3/render/src/case_runner.cpp and with
# check_render.REQUIRED_SIDECAR_SCHEMA. It travels in the provenance record so
# that a dump root written by an older harness is refused rather than read.
SIDECAR_SCHEMA = 3

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

RENDER_SOURCES = [
    "display_list.cpp",
    "scene.cpp",
    "surface.cpp",
    "cpu_raster_backend.cpp",
    "case_runner.cpp",
    "external_surface_reader.cpp",
    "glyph_outlines.cpp",
    "glyph_outline_rasterizer.cpp",
]

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
    parser.add_argument("--glyph-outlines", type=Path, default=None,
                        help="a recorded glyph-outline directory (CI artifact, see "
                             "fetch_measurements.py --glyph-outlines); families in it "
                             "paint from the recording instead of refusing, and with "
                             "--ink-font the recorded painting is compared against "
                             "DirectWrite's over the same file")
    parser.add_argument("--runtime-fonts", type=Path, default=None,
                        help="where to place the pinned open-source compatibility font "
                             "(default: <root>/runtime-fonts); it is what the DirectWrite "
                             "provider test resolves its alias case through")
    parser.add_argument("--no-runtime-fonts", action="store_true",
                        help="do not fetch the compatibility font; the provider test then "
                             "skips its private-alias case by name")
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
    dcomp_src = PHASE3_DIR / "render" / "dcomp"
    includes = ["-I" + str(layout_src), "-I" + str(render_src), "-I" + str(gdi_src),
                "-I" + str(dcomp_src)]

    # Static everything, for the reason build_xamlcore.py gives: a binary that
    # pulls in libstdc++-6.dll fails under Wine with a bare MOD_NOT_FOUND.
    common = ["x86_64-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall", "-Wextra",
              "-static", "-static-libgcc", "-static-libstdc++"]
    libraries = ["-lgdi32", "-luser32", "-lmsimg32", "-ldwrite", "-luuid"]

    sources = ([str(layout_src / name) for name in LAYOUT_SOURCES]
               + [str(layout_src / "markup.cpp")]
               + [str(render_src / name) for name in RENDER_SOURCES]
               + [str(gdi_src / "gdi_target.cpp"),
                  str(gdi_src / "dwrite_text_provider.cpp"),
                  str(gdi_src / "island_frame_cache.cpp")])

    harness = root / "render_cases_gdi.exe"
    run(common + ["-o", str(harness), str(gdi_src / "render_cases_gdi.cpp")]
        + sources + includes + libraries)

    host = root / "xaml_window.exe"
    run(common + ["-o", str(host), str(gdi_src / "xaml_window.cpp")]
        + sources + includes + libraries)

    ink = root / "ink_check.exe"
    run(common + ["-o", str(ink), str(gdi_src / "ink_check.cpp")]
        + sources + includes + libraries)

    outline_compare = root / "outline_compare.exe"
    run(common + ["-o", str(outline_compare), str(gdi_src / "outline_compare.cpp")]
        + sources + includes + libraries)

    frame_cache_test = root / "island_frame_cache_test.exe"
    run(common + ["-o", str(frame_cache_test),
                  str(gdi_src / "island_frame_cache_test.cpp")]
        + sources + includes + libraries)

    dwrite_test = root / "dwrite_text_provider_test.exe"
    run(common + ["-o", str(dwrite_test),
                  str(gdi_src / "dwrite_text_provider_test.cpp")]
        + sources + includes + libraries)

    # This test uses a deterministic fake compositor, but compiles and links
    # the production IDCompositionDesktopDevice/D3D11 adapter in the same
    # executable. It therefore catches Windows ABI drift without requiring a
    # working compositor from the Wine runner.
    dcomp_test = root / "dcomp_scene_backend_test.exe"
    dcomp_sources = [str(dcomp_src / "dcomp_scene_backend_test.cpp"),
                     str(dcomp_src / "dcomp_scene_backend.cpp"),
                     str(render_src / "cpu_raster_backend.cpp"),
                     str(render_src / "scene.cpp"),
                     str(render_src / "surface.cpp")]
    run(common + ["-o", str(dcomp_test)] + dcomp_sources + includes
        + ["-ldcomp", "-ld3d11", "-ldxgi", "-lole32"])

    if args.skip_run:
        print(f"built {harness}, {host}, {ink}, {outline_compare}, {frame_cache_test}, "
              f"{dwrite_test} and {dcomp_test}")
        return 0

    environment = os.environ.copy()
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEDEBUG"] = environment.get("WINEDEBUG", "-all")
    if not (prefix / "system.reg").is_file():
        run(["wineboot", "-u"], env=environment)

    # The pinned open-source compatibility font, for the provider test and for
    # the provider test only.
    #
    # It is deliberately *not* in the corpus harness's environment. The manifest
    # aliases Segoe Fluent Icons and Segoe MDL2 Assets onto the Uno Fluent Icons
    # file, and that alias is sound on the numbers -- both harvested Segoe icon
    # families are 2048 units per em, ascent 2048, descent 0, line gap 0, and
    # advance 2048 for every one of the 44 codepoints Terminal's markup names,
    # and the Uno file matches all of it -- but it would buy the corpus nothing
    # and cost it clarity. Nothing: the fifteen icon cases are every one of them
    # a FontIcon, and display_list.cpp emits no glyph op for a FontIcon at all
    # (it contributes a refusal or nothing), so no icon ink is drawn in any
    # backend, native included. Clarity: handing the harness a manifest it has
    # no use for would put a substitute face in the environment of a gate that
    # does not need one.
    #
    # Segoe UI has no such stand-in in any case. Nothing open is metrically
    # identical to it, so its text refuses by name; see phase3/render/README.md.
    provider_test_environment = dict(environment)
    if not args.no_runtime_fonts:
        runtime_fonts = args.runtime_fonts or (root / "runtime-fonts")
        try:
            manifest = prepare_runtime_fonts(RUNTIME_FONT_SPEC, runtime_fonts.resolve(),
                                             "wine")
        except Exception as failure:  # noqa: BLE001 - reported, not swallowed
            print(f"::notice::the pinned runtime fonts are unavailable ({failure}); "
                  "the provider's private-alias path goes unchecked in this run")
        else:
            provider_test_environment["OPENXAML_FONT_ALIAS_MANIFEST"] = windows_path(manifest)
            print(f"::notice::runtime fonts: {manifest}")

    # Most of this test needs no display -- memory DCs and DIB sections do not
    # -- and that used to be true of all of it. It stopped being true in wave 6:
    # LayeredChildCompositesOverItsParent creates a real popup parent and a
    # WS_EX_LAYERED child (island_frame_cache_test.cpp), and without a display
    # those CreateWindowExW calls return null and the CHECKs fail. So the
    # display is a requirement of running it, and a run without one says so by
    # name instead of reporting a window-server failure as a renderer bug.
    if environment.get("DISPLAY"):
        run(["wine", str(frame_cache_test)], env=environment)
    else:
        print("::notice::no DISPLAY; the island frame-cache test creates real windows "
              "for its layered-child case, so it is skipped by name rather than passed")
    # The ink font is handed to the provider test too, so the private-font path
    # the ink samples depend on is checked by a test that says which assertion
    # failed, rather than only by a tool that paints or does not.
    dwrite_arguments = ([windows_path(args.ink_font.resolve()), args.ink_family]
                        if args.ink_font else [])
    run(["wine", str(dwrite_test)] + dwrite_arguments, env=provider_test_environment)
    run(["wine", str(dcomp_test)], env=environment)

    # Say out loud what the corpus is about to be measured against. A fonts
    # directory holding no per-family metrics does not fail the run: every text
    # and icon case loads with "no harvested metrics for the font family ..."
    # and lands in the checker's "not laid out" column, where a pass over the
    # remainder looks the same as a pass over the corpus. Same for the theme
    # dictionary and the level 5 and 7 cases that name a resource key.
    families = sorted(p.stem for p in args.fonts.glob("*.json"))
    print(f"::notice::fonts: {len(families)} harvested famil(ies) in {args.fonts}"
          + (f" -- {', '.join(families)}" if families else
             " -- none; every text and icon case will refuse to lay out"))
    dictionaries = sorted(p.name for p in args.theme_resources.glob("*.json")) \
        if args.theme_resources.is_dir() else []
    print(f"::notice::theme resources: {len(dictionaries)} dictionar(ies) in "
          f"{args.theme_resources}"
          + ("" if dictionaries else
             " -- none; every case naming a WinUI resource key will refuse to lay out"))
    outline_documents = []
    if args.glyph_outlines is not None:
        if not args.glyph_outlines.is_dir():
            print(f"::error::--glyph-outlines {args.glyph_outlines} is not a directory")
            return 4
        outline_documents = sorted(p.stem for p in args.glyph_outlines.glob("*.json"))
        print(f"::notice::glyph outlines: {len(outline_documents)} recorded famil(ies) in "
              f"{args.glyph_outlines}"
              + (f" -- {', '.join(outline_documents)}" if outline_documents else
                 " -- none; every family keeps painting through DirectWrite or refusing"))

    # A window read-back from a previous run would otherwise survive into this
    # one and be compared, pixel for pixel, against dumps it has nothing to do
    # with. The dumps directory is already recreated below for that reason; the
    # read-backs live beside it and need saying so explicitly.
    for stale in root.glob("*.window.ppm"):
        stale.unlink()

    results = root / "gdi-dumps"
    if results.exists():
        shutil.rmtree(results)
    harness_arguments = ["wine", str(harness), str(args.cases.resolve()), str(results),
                         str(args.fonts.resolve()), str(args.theme_resources.resolve())]
    if args.glyph_outlines is not None:
        harness_arguments += ["--glyph-outlines", str(args.glyph_outlines.resolve())]
    run(harness_arguments, env=environment)

    # Written after the dumps, into the directory that was just recreated, so a
    # record can never outlive or precede the dumps it speaks for. The Wine gate
    # refuses a dump root whose record does not match this checkout; without it
    # the gate cannot tell fresh dumps from two-wave-old ones. See
    # render_provenance.py.
    provenance = render_provenance.write(
        results,
        render_provenance.record(
            repository=REPO_DIR,
            sources=render_provenance.source_roots(PHASE3_DIR),
            cases=args.cases,
            fonts=args.fonts, theme_resources=args.theme_resources,
            sidecar_schema=SIDECAR_SCHEMA,
            glyph_outlines=args.glyph_outlines))
    print(f"dumps in {results}; provenance in {provenance}")

    if args.ink_font:
        ink_dumps = root / "ink-dumps"
        if ink_dumps.exists():
            shutil.rmtree(ink_dumps)
        # A DOS path, not the native one: CreateFontFileReference is a Windows
        # API and takes a Windows path, and a run that passed the unix path got
        # as far as GDI accepting the file and DirectWrite never seeing it.
        completed = run(["wine", str(ink), windows_path(args.ink_font.resolve()),
                         args.ink_family, str(args.fonts.resolve()), str(ink_dumps)],
                        env=environment, check=False)
        if completed.returncode != 0:
            print("::error::the ink samples did not paint")
            return 5
        print(f"ink dumps in {ink_dumps}")

    # The recorded-versus-live comparison the glyph-outlines README calls for:
    # the same runs painted twice, once through DirectWrite with the font file
    # loaded privately, once through the recorded outlines, and the two
    # coverages compared. This is the only check that can say the recorded
    # shapes are the font's -- containment cannot -- and it needs both halves,
    # so it runs exactly when both were given.
    if args.ink_font and args.glyph_outlines is not None:
        document = None
        for candidate in sorted(args.glyph_outlines.glob("*.json")):
            recorded = json.loads(candidate.read_text())
            if str(recorded.get("family", "")).lower() == args.ink_family.lower():
                document = recorded
                break
        if document is None:
            print(f"::notice::no recorded outlines for \"{args.ink_family}\" in "
                  f"{args.glyph_outlines}; the recorded-versus-live comparison has "
                  "nothing to compare and is skipped by name")
        else:
            # Same file, not merely the same family: the recording carries the
            # SHA-256 of the file it was read from, and comparing against any
            # other file would be comparing two fonts.
            recorded_hash = str(document.get("source", {}).get("sha256", ""))
            actual_hash = hashlib.sha256(args.ink_font.read_bytes()).hexdigest()
            if recorded_hash != actual_hash:
                print(f"::error::the recorded outlines for \"{args.ink_family}\" were "
                      f"read off a file whose sha256 is {recorded_hash}, and "
                      f"{args.ink_font} is {actual_hash}; these are two different "
                      "fonts and comparing them would prove nothing")
                return 5
            comparison_report = root / "outline-comparison.json"
            comparison_report.unlink(missing_ok=True)
            completed = run(["wine", str(outline_compare),
                             windows_path(args.ink_font.resolve()), args.ink_family,
                             str(args.glyph_outlines.resolve()),
                             str(comparison_report)],
                            env=environment, check=False)
            if completed.returncode != 0:
                print("::error::the recorded outlines do not reproduce DirectWrite's "
                      "coverage over the same font file")
                return 5
            print(f"outline comparison in {comparison_report}")

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
