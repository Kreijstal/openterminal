#!/usr/bin/env python3
"""Build the WinRT layout DLL, register it in a Wine prefix, and measure.

The whole pipeline in one command:

  1. fetch and extract the pinned Windows SDK (shared with phase2, one pin)
  2. rewrite its headers into something GCC accepts
  3. harvest IIDs and generate the E_NOTIMPL bases from them
  4. cross-compile openxaml.dll and the ABI client with mingw-w64
  5. register the runtime classes in a Wine prefix
  6. run the corpus through Wine and write results

Nothing downloaded or built enters the repository: everything lands under a
/tmp root, as the other phases do.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

PHASE3_DIR = Path(__file__).resolve().parent.parent
REPOSITORY_ROOT = PHASE3_DIR.parent

# One SDK pin for the repository, declared by phase2. Importing its fetcher
# rather than copying it means this cannot drift to a different SDK than the
# one Terminal itself is built against.
sys.path.insert(0, str(REPOSITORY_ROOT / "phase2" / "scripts"))
from build_mingw import (  # noqa: E402
    download_checked,
    ensure_tmp_root,
    require_tool,
)
import json  # noqa: E402

# The layout core, which both the DLL and the client link. json.cpp and
# fonts.cpp are in it because text measurement reads harvested metrics, and
# those arrive as JSON.
#
# The whole library rather than the subset the DLL's own classes need, and
# deliberately: the client links markup.cpp, which reaches every type the
# parser can build, and a list that tracked only the DLL would break the
# client every time a type was added. It is the same list as
# phase3/layout/CMakeLists.txt and has to stay in step with it -- a source
# missing from here is a link failure naming the symbol, which is the failure
# mode worth having.
LAYOUT_SOURCES = ["property.cpp", "element.cpp", "border.cpp", "control.cpp",
                  "stack_panel.cpp", "grid.cpp", "chrome.cpp", "canvas.cpp",
                  "content_presenter.cpp", "geometry.cpp", "image.cpp", "shape.cpp",
                  "icon.cpp", "brush.cpp", "text.cpp", "fonts.cpp", "json.cpp",
                  "scroll_viewer.cpp", "basic_controls.cpp",
                  "resources.cpp", "style.cpp", "xdirectives.cpp", "resw_strings.cpp"]
LAYOUT_SOURCES += ["binding.cpp", "visual_state.cpp", "default_styles.cpp",
                   "advanced_controls.cpp"]

# The classes the DLL claims. Registering a class it does not implement would
# route a caller to us and then fail at DllGetActivationFactory, which is worse
# than not being registered at all.
RUNTIME_CLASSES = [
    "Windows.UI.Xaml.Controls.Border",
    "Windows.UI.Xaml.Controls.Grid",
    "Windows.UI.Xaml.Controls.StackPanel",
    "Windows.UI.Xaml.Controls.Canvas",
    "Windows.UI.Xaml.Controls.ContentPresenter",
    "Windows.UI.Xaml.Controls.Image",
    "Windows.UI.Xaml.Controls.PathIcon",
    "Windows.UI.Xaml.Shapes.Path",
    "Windows.UI.Xaml.Controls.TextBlock",
    "Windows.UI.Xaml.Controls.ColumnDefinition",
    "Windows.UI.Xaml.Controls.RowDefinition",
    "Windows.UI.Xaml.Controls.Primitives.LayoutInformation",
    "Windows.UI.Xaml.Controls.ContentControl",
    "Windows.UI.Xaml.Controls.Page",
    "Windows.UI.Xaml.Controls.Frame",
    "Windows.UI.Xaml.Controls.ItemsControl",
    "Windows.UI.Xaml.Controls.ListView",
    "Windows.UI.Xaml.Controls.Primitives.Popup",
    "Windows.UI.Xaml.Controls.Button",
    "Windows.UI.Xaml.Controls.TextBox",
    "Windows.UI.Xaml.Controls.ToolTip",
    "Windows.UI.Xaml.Controls.Primitives.Thumb",
    "Windows.UI.Xaml.Controls.ScrollViewer",
    "Windows.UI.Xaml.Controls.FontIcon",
    "Windows.UI.Xaml.Shapes.Rectangle",
    # Not a control: a TextBlock's FontFamily is an object, so the ABI needs a
    # class to make one with.
    "Windows.UI.Xaml.Media.FontFamily",
    # Not a control either, and not activatable: a static-only class holding
    # the operations Windows.UI.Xaml.Duration cannot carry itself.
    "Windows.UI.Xaml.DurationHelper",
    # Composable rather than activatable. A host derives its own App from this
    # one, so what the caller reaches is IApplicationFactory::CreateInstance.
    "Windows.UI.Xaml.Application",
]


def run(arguments: list[str], env: dict[str, str] | None = None) -> None:
    print("+ " + " ".join(arguments), flush=True)
    subprocess.run(arguments, check=True, env=env)


def fetch_sdk(root: Path) -> Path:
    """Returns the extracted SDK's versioned Include directory.

    phase2 extracts only the tools and WinMDs it needs, because it gets its
    headers from a C++/WinRT projection. This DLL implements the ABI rather
    than consuming it, so it needs the generated headers and the IDL they came
    from -- the same package, a different subset of it.
    """
    pins = json.loads((REPOSITORY_ROOT / "phase2" / "upstreams.json").read_text())
    pin = pins["windows_sdk_cpp"]
    sdk_root = root / "windows-sdk-cpp"
    package = download_checked(
        pin["url"],
        sdk_root / f"{pin['id'].lower()}.{pin['version']}.nupkg",
        pin["sha256"],
    )

    destination = sdk_root / "extracted"
    include = destination / "c" / "Include" / pin["platform_version"]
    prefix = f"c/Include/{pin['platform_version']}/winrt/"
    if not (include / "winrt" / "windows.ui.xaml.controls.h").is_file():
        with zipfile.ZipFile(package) as archive:
            members = [name for name in archive.namelist()
                       if name.startswith(prefix) and not name.endswith("/")]
            if not members:
                raise SystemExit(f"the SDK package has no {prefix}")
            for member in sorted(members):
                target = destination / member
                # The archive is pinned by hash, but a path check costs
                # nothing and keeps an unexpected package from writing
                # outside the extraction root.
                if destination.resolve() not in target.resolve().parents:
                    raise SystemExit(f"unsafe archive member: {member}")
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source, target.open("wb") as sink:
                    shutil.copyfileobj(source, sink)
            print(f"extracted {len(members)} SDK headers", flush=True)
    return include


def wine_dll_path(path: Path) -> str:
    """A Wine registry DllPath: the Z: drive, with escaped backslashes."""
    return "Z:" + str(path).replace("/", "\\\\")


def registration(dll: Path) -> str:
    lines = ["Windows Registry Editor Version 5.00", ""]
    for name in RUNTIME_CLASSES:
        key = ("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\WindowsRuntime\\"
               f"ActivatableClassId\\{name}")
        lines += [
            f"[{key}]",
            f'"DllPath"="{wine_dll_path(dll)}"',
            # In-process server, single-threaded apartment, base trust: the
            # combination Wine's RoGetActivationFactory expects for a DLL it
            # loads into the caller's process.
            '"ActivationType"=dword:00000000',
            '"Threading"=dword:00000001',
            '"TrustLevel"=dword:00000000',
            "",
        ]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/tmp/openterminal-phase3-xamlcore"))
    parser.add_argument("--prefix", type=Path, default=None,
                        help="Wine prefix (default: <root>/wine-prefix)")
    parser.add_argument("--cases", type=Path, default=PHASE3_DIR / "xaml-db" / "cases")
    parser.add_argument("--results", type=Path, default=None,
                        help="where to write measurements (default: <root>/results)")
    parser.add_argument("--skip-run", action="store_true",
                        help="build and register, but do not measure")
    parser.add_argument("--fonts", type=Path, default=PHASE3_DIR / "xaml-db" / "fonts",
                        help="harvested font metrics; the DLL reads them from here")
    args = parser.parse_args()

    for tool in ("x86_64-w64-mingw32-g++", "wine"):
        require_tool(tool)

    root = ensure_tmp_root(args.root)
    prefix = args.prefix or root / "wine-prefix"
    results = args.results or root / "results"

    include = fetch_sdk(root)
    shadow = root / "sdk-headers"
    generated = root / "generated"
    run([sys.executable, str(PHASE3_DIR / "scripts" / "prepare_sdk_headers.py"),
         str(include), str(shadow)])
    run([sys.executable, str(PHASE3_DIR / "scripts" / "harvest_xaml_iids.py"),
         "--idl-dir", str(include / "winrt"),
         "--include-dir", str(shadow / "winrt"),
         "--interfaces", str(PHASE3_DIR / "xamlcore" / "iid-interfaces.txt"),
         "--output", str(generated / "openxaml_iids.h")])
    run([sys.executable, str(PHASE3_DIR / "scripts" / "generate_abi_stubs.py"),
         "--include-dir", str(shadow / "winrt"),
         "--interfaces", str(PHASE3_DIR / "xamlcore" / "abi-interfaces.txt"),
         "--output", str(generated / "openxaml_abi_stubs.h")])

    layout_src = PHASE3_DIR / "layout" / "src"
    core_src = PHASE3_DIR / "xamlcore" / "src"
    includes = ["-I" + str(shadow / "winrt"), "-I" + str(layout_src),
                "-I" + str(core_src), "-I" + str(generated)]
    # Static everything. A DLL that pulls in libstdc++-6.dll fails to load
    # under Wine with a bare MOD_NOT_FOUND, which reads as a missing DLL
    # rather than as a missing dependency of one.
    common = ["x86_64-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall",
              "-static", "-static-libgcc", "-static-libstdc++"]
    libraries = ["-lruntimeobject", "-lole32", "-luuid"]

    dll = root / "openxaml.dll"
    run(common + ["-shared", "-o", str(dll), str(core_src / "factory.cpp")]
        + [str(layout_src / name) for name in LAYOUT_SOURCES]
        + includes + libraries)

    client = root / "measure_cases_winrt.exe"
    run(common + ["-o", str(client),
                  str(PHASE3_DIR / "xamlcore" / "client" / "measure_cases_winrt.cpp"),
                  str(layout_src / "markup.cpp")]
        + [str(layout_src / name) for name in LAYOUT_SOURCES]
        + includes + libraries)

    wave34_smoke = root / "wave34_smoke.exe"
    run(common + ["-o", str(wave34_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" / "wave34_smoke.cpp")]
        + includes + libraries)

    environment = os.environ.copy()
    environment["WINEPREFIX"] = str(prefix)
    environment["WINEDEBUG"] = environment.get("WINEDEBUG", "-all")
    # A DLL has no corpus to find font metrics beside, so it is told where they
    # are. Absent metrics are not fatal: only the text cases need them, and
    # they say so individually when they measure.
    if args.fonts.is_dir():
        environment["OPENXAML_FONT_METRICS"] = "Z:" + str(args.fonts.resolve())
    else:
        print(f"no font metrics at {args.fonts}; the text cases will fail", flush=True)
    if not (prefix / "system.reg").is_file():
        run(["wineboot", "-u"], env=environment)

    registry_file = root / "openxaml.reg"
    registry_file.write_text(registration(dll), encoding="utf-8")
    run(["wine", "regedit", str(registry_file)], env=environment)
    run(["wine", str(wave34_smoke)], env=environment)

    if args.skip_run:
        print(f"built and registered {dll}")
        return

    run(["wine", str(client), str(args.cases), str(results)], env=environment)
    print(f"results in {results}")


if __name__ == "__main__":
    main()
