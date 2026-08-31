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
    ensure_checkout,
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
LAYOUT_SOURCES = ["property.cpp", "events.cpp", "element.cpp", "border.cpp", "control.cpp",
                  "stack_panel.cpp", "grid.cpp", "chrome.cpp", "canvas.cpp",
                  "content_presenter.cpp", "geometry.cpp", "image.cpp", "shape.cpp",
                  "icon.cpp", "brush.cpp", "text.cpp", "fonts.cpp", "json.cpp",
                  "scroll_viewer.cpp", "basic_controls.cpp",
                  "resources.cpp", "style.cpp", "xdirectives.cpp", "resw_strings.cpp"]
LAYOUT_SOURCES += ["binding.cpp", "visual_state.cpp", "default_styles.cpp",
                   "advanced_controls.cpp"]

# DesktopWindowXamlSource compiles an arranged layout tree into an immutable
# CPU frame and presents its cached DIB from WM_PAINT. Keep this in step with
# render/CMakeLists.txt and build_render.py.
RENDER_SOURCES = ["display_list.cpp", "scene.cpp", "surface.cpp",
                  "cpu_raster_backend.cpp", "case_runner.cpp",
                  "external_surface_reader.cpp",
                  "glyph_outlines.cpp", "glyph_outline_rasterizer.cpp"]
GDI_RENDER_SOURCES = ["gdi_target.cpp", "dwrite_text_provider.cpp",
                      "island_frame_cache.cpp"]
DCOMP_RENDER_SOURCES = ["dcomp_scene_backend.cpp"]

# The classes the DLL claims. Registering a class it does not implement would
# route a caller to us and then fail at DllGetActivationFactory, which is worse
# than not being registered at all.
RUNTIME_CLASSES = [
    "Windows.Foundation.Collections.ValueSet",
    "Windows.UI.Colors",
    "Windows.UI.ViewManagement.AccessibilitySettings",
    "Windows.UI.Xaml.VisualStateManager",
    "Windows.UI.Xaml.VisualState",
    "Windows.UI.Xaml.VisualStateGroup",
    "Windows.UI.Xaml.Setter",
    "Windows.UI.Xaml.Input.FocusManager",
    "Windows.UI.Xaml.DispatcherTimer",
    # The island thread's own window. Windows Terminal reads the modifier
    # keys through it on every keystroke; without this line the class resolves
    # to whatever else the prefix registers for it.
    "Windows.UI.Core.CoreWindow",
    "Windows.UI.Xaml.Media.Animation.Timeline",
    "Windows.UI.Xaml.Controls.Border",
    "Windows.UI.Xaml.Controls.Viewbox",
    "Windows.UI.Xaml.Controls.Panel",
    "Windows.UI.Xaml.Controls.Grid",
    "Windows.UI.Xaml.Controls.StackPanel",
    "Windows.UI.Xaml.Controls.Canvas",
    "Windows.UI.Xaml.Controls.ContentPresenter",
    "Windows.UI.Xaml.Controls.SwapChainPanel",
    "Windows.UI.Xaml.Controls.Image",
    "Windows.UI.Xaml.Controls.PathIcon",
    "Windows.UI.Xaml.Shapes.Path",
    "Windows.UI.Xaml.Controls.TextBlock",
    "Windows.UI.Xaml.Documents.Run",
    "Windows.UI.Xaml.Documents.LineBreak",
    "Windows.UI.Xaml.Data.PropertyChangedEventArgs",
    "Windows.UI.Xaml.Controls.ColumnDefinition",
    "Windows.UI.Xaml.Controls.RowDefinition",
    "Windows.UI.Xaml.Controls.Primitives.LayoutInformation",
    "Windows.UI.Xaml.Media.VisualTreeHelper",
    "Windows.UI.Xaml.DurationHelper",
    "Windows.UI.Xaml.GridLengthHelper",
    "Windows.UI.Xaml.Application",
    "Windows.UI.Xaml.ResourceDictionary",
    "Windows.UI.Xaml.Controls.UserControl",
    "Microsoft.UI.Xaml.Controls.XamlControlsResources",
    "Microsoft.UI.Xaml.Controls.TabView",
    "Microsoft.UI.Xaml.Controls.NavigationView",
    "Microsoft.UI.Xaml.Controls.NavigationViewItem",
    "Microsoft.UI.Xaml.Controls.NavigationViewItemHeader",
    "Microsoft.UI.Xaml.Controls.BreadcrumbBar",
    "Microsoft.UI.Xaml.Controls.TabViewItem",
    "Microsoft.UI.Xaml.Controls.SplitButton",
    "Microsoft.UI.Xaml.Controls.CommandBarFlyout",
    "Microsoft.UI.Xaml.Controls.ProgressRing",
    "Microsoft.UI.Xaml.Controls.NumberBox",
    "Microsoft.UI.Xaml.Controls.InfoBar",
    "Microsoft.UI.Xaml.Controls.InfoBadge",
    "Microsoft.UI.Xaml.Controls.BitmapIconSource",
    "Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider",
    "Windows.System.DispatcherQueue",
    "Windows.UI.Xaml.Hosting.WindowsXamlManager",
    "Windows.ApplicationModel.Resources.Core.ResourceManager",
    "Windows.ApplicationModel.Resources.Core.ResourceContext",
    "Windows.UI.Xaml.Hosting.DesktopWindowXamlSource",
    # The property system's own classes. DependencyProperty is static-only --
    # RoActivateInstance on it fails, as it does on the real one -- but its
    # factory is where Register and RegisterAttached live, and a caller reaches
    # a factory by activating the class name.
    "Windows.UI.Xaml.DependencyProperty",
    "Windows.UI.Xaml.PropertyMetadata",
    "Windows.UI.Xaml.DataTemplate",
    "Windows.UI.Xaml.Controls.ContentControl",
    "Windows.UI.Xaml.Controls.ContentDialog",
    "Windows.UI.Xaml.Controls.Page",
    "Windows.UI.Xaml.Controls.Frame",
    "Windows.UI.Xaml.Controls.ComboBox",
    "Windows.UI.Xaml.Controls.ToggleSwitch",
    "Windows.UI.Xaml.Controls.CheckBox",
    "Windows.UI.Xaml.Controls.ItemsControl",
    "Windows.UI.Xaml.Controls.ListView",
    "Windows.UI.Xaml.Controls.Primitives.Popup",
    "Windows.UI.Xaml.Controls.MenuFlyout",
    "Windows.UI.Xaml.Controls.MenuFlyoutItem",
    "Windows.UI.Xaml.Controls.MenuFlyoutSeparator",
    "Windows.UI.Xaml.Controls.MenuFlyoutSubItem",
    "Windows.UI.Xaml.Controls.BitmapIconSource",
    "Windows.UI.Xaml.Controls.IconSourceElement",
    "Windows.UI.Xaml.Controls.ToolTipService",
    "Windows.UI.Xaml.Controls.Primitives.FlyoutBase",
    "Windows.UI.Xaml.Automation.AutomationProperties",
    "Windows.UI.Text.FontWeights",
    "Windows.UI.Xaml.Controls.Button",
    "Windows.UI.Xaml.Controls.AppBarButton",
    "Windows.UI.Xaml.Controls.TextBox",
    "Windows.UI.Xaml.Controls.AutoSuggestBox",
    "Windows.UI.Xaml.Controls.ToolTip",
    "Windows.UI.Xaml.Controls.Primitives.Thumb",
    "Windows.UI.Xaml.Controls.Primitives.ScrollBar",
    "Windows.UI.Xaml.Controls.ScrollViewer",
    "Windows.UI.Xaml.Controls.FontIcon",
    "Windows.UI.Xaml.Controls.SymbolIcon",
    "Windows.UI.Xaml.Shapes.Rectangle",
    # Not a control: a TextBlock's FontFamily is an object, so the ABI needs a
    # class to make one with.
    "Windows.UI.Xaml.Media.FontFamily",
    "Windows.UI.Xaml.Media.ImageBrush",
    "Windows.UI.Xaml.Media.ScaleTransform",
    "Windows.UI.Xaml.Media.SolidColorBrush",
]


# Nobody is at this keyboard. Wine's addon installer asks for Wine Mono while
# it creates a prefix whenever the build ships without it -- WineHQ's packages
# do, Debian's patch the prompt out -- and with a display present that ask is a
# modal window: wineboot then waits five minutes for a click, is torn down, and
# reports "could not load kernel32.dll, status c0000135" with exit 53, naming
# nothing that happened. Declining mscoree and mshtml costs this runtime
# nothing, since it implements a XAML ABI and touches neither .NET nor MSHTML,
# and it is what the headless boots were silently getting all along. winedbg
# goes off for the reason phase4's scripts give: a crashed probe must report
# rather than sit in a debugger.
WINE_DLL_OVERRIDES = "winedbg.exe=d;mscoree,mshtml="


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
    # One hosting header sdk.h consumes lives under um/ rather than winrt/.
    # Dev machines never notice because current mingw-w64 ships its own copy,
    # but the mingw-w64 11 on ubuntu-24.04 does not, so it comes from the
    # pinned package like every other generated header -- with its IDL, the
    # same way the winrt subset carries them.
    um_members = tuple(
        f"c/Include/{pin['platform_version']}/um/"
        f"windows.ui.xaml.hosting.desktopwindowxamlsource{suffix}"
        for suffix in (".h", ".idl"))
    already_extracted = (
        (include / "winrt" / "windows.ui.xaml.controls.h").is_file()
        and all((destination / member).is_file() for member in um_members))
    if not already_extracted:
        with zipfile.ZipFile(package) as archive:
            members = [name for name in archive.namelist()
                       if ((name.startswith(prefix) or name in um_members)
                           and not name.endswith("/"))]
            if not members:
                raise SystemExit(f"the SDK package has no {prefix}")
            missing_um = [name for name in um_members if name not in members]
            if missing_um:
                raise SystemExit(
                    "the SDK package is missing " + ", ".join(missing_um))
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
    parser.add_argument("--dll-only", action="store_true",
                        help="build only openxaml.dll; keep the existing registration")
    parser.add_argument("--register-only", action="store_true",
                        help="register an already-built openxaml.dll")
    parser.add_argument("--fonts", type=Path, default=PHASE3_DIR / "xaml-db" / "fonts",
                        help="harvested font metrics; the DLL reads them from here")
    parser.add_argument("--no-xvfb", action="store_true",
                        help="do not re-exec inside Xvfb even without a DISPLAY")
    args = parser.parse_args()

    # One X server must cover prefix initialization, registration and every
    # probe, the way run_terminal_integration.py already does it. Wine decides
    # a session's display driver once, when the first process in it creates the
    # desktop window, and every later process in that same wineserver session
    # inherits that decision: a prefix booted and registered without a DISPLAY
    # gets the null driver, and then CreateWindowEx returns NULL in the window
    # probes even though xvfb-run handed them a display of their own. Re-exec
    # the whole command once inside Xvfb instead of wrapping single probes.
    if not args.no_xvfb and not os.environ.get("DISPLAY"):
        require_tool("xvfb-run")
        raise SystemExit(subprocess.run(
            ["xvfb-run", "--auto-servernum", sys.executable, "-B",
             str(Path(__file__).resolve()), *sys.argv[1:], "--no-xvfb"],
            check=False).returncode)

    for tool in ("x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-windres", "wine"):
        require_tool(tool)

    root = ensure_tmp_root(args.root)
    prefix = args.prefix or root / "wine-prefix"
    results = args.results or root / "results"

    if args.register_only:
        dll = root / "openxaml.dll"
        if not dll.is_file():
            raise SystemExit(f"no DLL to register at {dll}")
        environment = os.environ.copy()
        environment["WINEPREFIX"] = str(prefix)
        environment["WINEDEBUG"] = environment.get("WINEDEBUG", "-all,+winediag")
        environment["WINEDLLOVERRIDES"] = environment.get(
            "WINEDLLOVERRIDES", WINE_DLL_OVERRIDES)
        registry_file = root / "openxaml.reg"
        registry_file.write_text(registration(dll), encoding="utf-8")
        run(["wine", "regedit", str(registry_file)], env=environment)
        print(f"registered {dll}")
        return

    include = fetch_sdk(root)
    shadow = root / "sdk-headers"
    generated = root / "generated"
    pins = json.loads((REPOSITORY_ROOT / "phase2" / "upstreams.json").read_text())
    xaml_source = ensure_checkout(
        pins["windows_ui_xaml_source"], root / "windows-ui-xaml-source"
    )
    run([
        sys.executable,
        str(PHASE3_DIR / "scripts" / "generate_stable_xbf_schema.py"),
        str(xaml_source / "dxaml" / "xcp" / "tools" / "XbfParser" /
            "WidgetSpinner" / "Metadata" / "StableXbfIndexMetadata.g.cs"),
        str(generated / "stable_xbf_schema.h"),
    ])
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
    render_src = PHASE3_DIR / "render" / "src"
    render_gdi = PHASE3_DIR / "render" / "gdi"
    render_dcomp = PHASE3_DIR / "render" / "dcomp"
    core_src = PHASE3_DIR / "xamlcore" / "src"
    # shadow/um holds only the hosting header fetch_sdk singles out, so it
    # cannot shadow mingw-w64's own um headers the way a full um/ tree would.
    includes = ["-I" + str(shadow / "winrt"), "-I" + str(shadow / "um"),
                "-I" + str(layout_src),
                "-I" + str(render_src), "-I" + str(render_gdi),
                "-I" + str(render_dcomp),
                "-I" + str(core_src), "-I" + str(generated)]
    # Static everything. A DLL that pulls in libstdc++-6.dll fails to load
    # under Wine with a bare MOD_NOT_FOUND, which reads as a missing DLL
    # rather than as a missing dependency of one.
    common = ["x86_64-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall",
              "-Wa,-mbig-obj",
              "-DOPENXAML_STABLE_XBF_SCHEMA",
              "-static", "-static-libgcc", "-static-libstdc++"]
    libraries = ["-lruntimeobject", "-lole32", "-loleaut32", "-luuid", "-lgdi32",
                 "-luser32", "-lmsimg32", "-ldwrite", "-ldcomp",
                 "-ld3d11", "-ldxgi", "-lwindowscodecs"]

    dll = root / "Microsoft.UI.Xaml.dll"
    import_library = root / "libopenxaml.dll.a"
    run(common + ["-shared", "-o", str(dll),
                  f"-Wl,--out-implib,{import_library}",
                  str(core_src / "factory.cpp"),
                  str(core_src / "core_dispatcher.cpp"),
                  str(core_src / "island_input_manager.cpp"),
                  str(core_src / "xaml_focus.cpp"),
                  str(core_src / "external_surface_binding.cpp"),
                  str(core_src / "resource_catalog.cpp"),
                  str(core_src / "xbf.cpp"), str(core_src / "xbf_object.cpp")]
        + [str(layout_src / name) for name in LAYOUT_SOURCES]
        # case_runner owns the shared scene-rasterization adapter and its
        # case-loading half references the layout markup loader.
        + [str(layout_src / "markup.cpp")]
        + [str(render_src / name) for name in RENDER_SOURCES]
        + [str(render_gdi / name) for name in GDI_RENDER_SOURCES]
        + [str(render_dcomp / name) for name in DCOMP_RENDER_SOURCES]
        + includes + libraries)
    # Preserve the historical local probe name while making the PE/import
    # library agree with the filename used by normal Windows deployments.
    shutil.copy2(dll, root / "openxaml.dll")

    if args.dll_only:
        print(f"built {dll}")
        return

    client = root / "measure_cases_winrt.exe"
    run(common + ["-o", str(client),
                  str(PHASE3_DIR / "xamlcore" / "client" / "measure_cases_winrt.cpp"),
                  str(layout_src / "markup.cpp")]
        + [str(layout_src / name) for name in LAYOUT_SOURCES]
        + includes + libraries)

    wave34_smoke = root / "wave34_smoke.exe"
    run(common + ["-o", str(wave34_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" / "wave34_smoke.cpp"),
                  # The live-brush check attaches a real render invalidation
                  # sink to the projected layout element. Keep this focused:
                  # these are the three translation units that own Element's
                  # visual-tree/sink and dependency-object implementation.
                  str(layout_src / "element.cpp"),
                  str(layout_src / "property.cpp"),
                  str(layout_src / "events.cpp")]
        + includes + libraries)

    island_render_smoke = root / "island_render_smoke.exe"
    run(common + ["-o", str(island_render_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "island_render_smoke.cpp")]
        + includes + libraries)

    # The pixel half of the phase-4 visible-UI gate. It links nothing of this
    # runtime on purpose: it reads desktop pixels out of whatever window is on
    # screen, so its readings are independent of the process being observed.
    terminal_pixel_probe = root / "terminal_pixel_probe.exe"
    run(common + ["-municode", "-o", str(terminal_pixel_probe),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "terminal_pixel_probe.cpp")]
        + ["-lgdi32", "-luser32"])

    # The injecting half of the phase-4 input round-trip gate. Like the pixel
    # probe it links nothing of this runtime: it finds the hosting window,
    # types into it by one named mechanism, and reports what it did.
    terminal_input_probe = root / "terminal_input_probe.exe"
    run(common + ["-municode", "-o", str(terminal_input_probe),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "terminal_input_probe.cpp")]
        + ["-lgdi32", "-luser32"])

    island_input_smoke = root / "island_input_smoke.exe"
    run(common + ["-o", str(island_input_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "island_input_smoke.cpp"),
                  str(core_src / "island_input_manager.cpp")]
        + includes + libraries)

    keyboard_input_smoke = root / "keyboard_input_smoke.exe"
    run(common + ["-o", str(keyboard_input_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "keyboard_input_smoke.cpp")]
        + includes + libraries)

    pointer_input_smoke = root / "pointer_input_smoke.exe"
    run(common + ["-o", str(pointer_input_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "pointer_input_smoke.cpp")]
        + includes + libraries)

    tap_input_smoke = root / "tap_input_smoke.exe"
    run(common + ["-o", str(tap_input_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "tap_input_smoke.cpp")]
        + includes + libraries)

    split_button_input_smoke = root / "split_button_input_smoke.exe"
    run(common + ["-o", str(split_button_input_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "split_button_input_smoke.cpp")]
        + includes + libraries)

    pointer_routing_test = root / "pointer_routing_test.exe"
    run(common + ["-o", str(pointer_routing_test),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "pointer_routing_test.cpp"),
                  str(core_src / "island_input_manager.cpp"),
                  str(core_src / "xaml_focus.cpp")]
        + [str(layout_src / name) for name in
           ["property.cpp", "events.cpp", "element.cpp", "canvas.cpp",
            "binding.cpp", "visual_state.cpp"]]
        + includes + libraries)

    tap_routing_test = root / "tap_routing_test.exe"
    run(common + ["-o", str(tap_routing_test),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "tap_routing_test.cpp"),
                  str(core_src / "island_input_manager.cpp"),
                  str(core_src / "xaml_focus.cpp")]
        + [str(layout_src / name) for name in
           ["property.cpp", "events.cpp", "element.cpp", "canvas.cpp",
            "binding.cpp", "visual_state.cpp"]]
        + includes + libraries)

    core_dispatcher_test = root / "core_dispatcher_test.exe"
    run(common + ["-o", str(core_dispatcher_test),
                  str(PHASE3_DIR / "xamlcore" / "tests" /
                      "core_dispatcher_test.cpp"),
                  str(core_src / "core_dispatcher.cpp")]
        + includes + libraries)

    resource_catalog_test = root / "resource_catalog_test.exe"
    run(common + ["-o", str(resource_catalog_test),
                  str(PHASE3_DIR / "xamlcore" / "tests" /
                      "resource_catalog_test.cpp"),
                  str(core_src / "resource_catalog.cpp"),
                  str(layout_src / "json.cpp")]
        + includes + libraries)

    # Binds to the SDK's vtable slots by hand rather than to this runtime's
    # own headers: a test that shares its subject's mistakes cannot find them.
    core_window_test = root / "core_window_test.exe"
    run(common + ["-o", str(core_window_test),
                  str(PHASE3_DIR / "xamlcore" / "tests" /
                      "core_window_test.cpp")]
        + includes + libraries)

    mux_bitmap_icon_test = root / "mux_bitmap_icon_source_factory_test.exe"
    run(common + ["-o", str(mux_bitmap_icon_test),
                  str(PHASE3_DIR / "xamlcore" / "tests" /
                      "mux_bitmap_icon_source_factory_test.cpp")]
        + includes + libraries)

    tab_view_selection_smoke = root / "tab_view_selection_smoke.exe"
    run(common + ["-o", str(tab_view_selection_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "tab_view_selection_smoke.cpp")]
        + includes + libraries)

    # One binary is run in two fresh processes on Windows: once with the
    # pinned official WinUI 2 DLL and once with this build's DLL. It uses
    # DllGetActivationFactory explicitly, so the result cannot accidentally
    # come from a package registration or a stale app-local deployment.
    muxc_tabview_oracle = root / "muxc_tabview_oracle.exe"
    muxc_tabview_manifest = root / "muxc_tabview_oracle_manifest.o"
    run(["x86_64-w64-mingw32-windres",
         "-I", str(PHASE3_DIR / "harness"),
         str(PHASE3_DIR / "xamlcore" / "client" /
             "muxc_tabview_oracle.rc"),
         str(muxc_tabview_manifest)])
    # Keep this ABI-only client at O0. GCC 16 devirtualizes the hand-declared
    # Microsoft.UI.Xaml interface (whose implementation lives in a separately
    # loaded PE) into an invalid direct call at O2; the raw ABI being measured
    # is independent of client optimization. The loaded DLL keeps its normal
    # optimization; only this observation client is compiled conservatively.
    run(common + ["-O0", "-municode", "-o", str(muxc_tabview_oracle),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "muxc_tabview_oracle.cpp"),
                  str(muxc_tabview_manifest)]
        + includes + libraries)

    external_surface_test = root / "external_surface_binding_test.exe"
    run(common + ["-o", str(external_surface_test),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "external_surface_binding_test.cpp"),
                  str(core_src / "external_surface_binding.cpp")]
        + includes + libraries)

    focus_smoke = root / "focus_smoke.exe"
    run(common + ["-o", str(focus_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "focus_smoke.cpp")]
        + includes + libraries)

    scrollbar_range_smoke = root / "scrollbar_range_smoke.exe"
    run(common + ["-o", str(scrollbar_range_smoke),
                  str(PHASE3_DIR / "xamlcore" / "client" /
                      "scrollbar_range_smoke.cpp")]
        + includes + libraries)

    environment = os.environ.copy()
    environment["WINEPREFIX"] = str(prefix)
    # winediag stays on. A plain -all silenced "Application tried to create a
    # window, but no driver could be loaded", and the probes then failed with
    # nothing but null window handles to show for it -- the channel Wine
    # reserves for environment faults is exactly the one worth keeping.
    environment["WINEDEBUG"] = environment.get("WINEDEBUG", "-all,+winediag")
    environment["WINEDLLOVERRIDES"] = environment.get(
        "WINEDLLOVERRIDES", WINE_DLL_OVERRIDES)
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
    # The window probes need a display; they get it from the one Xvfb this
    # command re-execed itself into, which the prefix was also booted and
    # registered under, so they share the session's x11 driver.
    run(["wine", str(island_input_smoke)], env=environment)
    run(["wine", str(keyboard_input_smoke)], env=environment)
    run(["wine", str(pointer_input_smoke)], env=environment)
    run(["wine", str(pointer_routing_test)], env=environment)
    run(["wine", str(tap_input_smoke)], env=environment)
    run(["wine", str(split_button_input_smoke)], env=environment)
    run(["wine", str(tap_routing_test)], env=environment)
    run(["wine", str(core_dispatcher_test)], env=environment)
    run(["wine", str(resource_catalog_test)], env=environment)
    # Also after registration: it asks RoGetActivationFactory for
    # Windows.UI.Core.CoreWindow, and an unregistered prefix answers with
    # whatever else claims that class.
    run(["wine", str(core_window_test)], env=environment)
    # This probe asks RoGetActivationFactory for the MUX factory and therefore
    # must run only after openxaml.dll has been registered in this prefix.
    run(["wine", str(mux_bitmap_icon_test)], env=environment)
    run(["wine", str(tab_view_selection_smoke)], env=environment)
    run(["wine", str(external_surface_test)], env=environment)
    run(["wine", str(focus_smoke)], env=environment)
    run(["wine", str(scrollbar_range_smoke)], env=environment)
    run(["wine", str(island_render_smoke)], env=environment)

    if args.skip_run:
        print(f"built and registered {dll}")
        return

    run(["wine", str(client), str(args.cases), str(results)], env=environment)
    print(f"results in {results}")


if __name__ == "__main__":
    main()
