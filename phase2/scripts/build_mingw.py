#!/usr/bin/env python3
"""Build pinned Terminal dependencies and native libraries with mingw-w64.

All downloaded packages and generated binaries are kept below a /tmp root.
The source and tool revisions come from phase2/upstreams.json.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path
from typing import Any


PHASE2_DIR = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PHASE2_DIR.parent
PINS_FILE = PHASE2_DIR / "upstreams.json"


def run(arguments: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print(f"+ {shlex.join(arguments)}", flush=True)
    subprocess.run(arguments, cwd=cwd, env=env, check=True)


def output(arguments: list[str]) -> str:
    return subprocess.check_output(arguments, text=True).strip()


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"required tool is not on PATH: {name}")


def ensure_tmp_root(path: Path) -> Path:
    resolved = path.resolve()
    tmp = Path("/tmp").resolve()
    if resolved == tmp or tmp not in resolved.parents:
        raise RuntimeError(f"build root must be a child of /tmp: {resolved}")
    resolved.mkdir(parents=True, exist_ok=True)
    return resolved


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_checked(url: str, destination: Path, expected_sha256: str) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        partial = destination.with_name(f"{destination.name}.partial")
        print(f"+ download {url} -> {destination}", flush=True)
        with urllib.request.urlopen(url) as response, partial.open("wb") as output_stream:
            shutil.copyfileobj(response, output_stream)
        partial.replace(destination)

    actual_sha256 = sha256_file(destination)
    if actual_sha256 != expected_sha256:
        raise RuntimeError(
            f"SHA-256 mismatch for {destination}: {actual_sha256}; expected {expected_sha256}"
        )
    return destination


def extract_windows_sdk(package: Path, destination: Path, platform_version: str) -> None:
    binary_prefix = f"c/bin/{platform_version}/x64/"
    xaml_prefix = f"c/bin/{platform_version}/XamlCompiler/"
    include_prefix = f"c/Include/{platform_version}/winrt/"
    platform_prefix = f"c/Platforms/UAP/{platform_version}/"
    references_prefix = f"c/References/{platform_version}/"
    union_metadata = f"c/UnionMetadata/{platform_version}/Windows.winmd"
    facade_metadata = f"c/UnionMetadata/{platform_version}/Facade/windows.winmd"
    required_members = {
        f"{binary_prefix}d3dcompiler_47.dll",
        f"{binary_prefix}fxc.exe",
        f"{binary_prefix}mdmerge.exe",
        f"{binary_prefix}midlrt.exe",
        f"{binary_prefix}midlrtmd.dll",
        f"{xaml_prefix}Microsoft.UI.Xaml.Markup.winmd",
        f"{xaml_prefix}Microsoft.Windows.UI.Xaml.Build.Tasks.dll",
        f"{xaml_prefix}Microsoft.Windows.UI.Xaml.Common.targets",
        f"{xaml_prefix}x64/genxbf.dll",
        f"{include_prefix}WinRTBase.idl",
        f"{include_prefix}midlbase.idl",
        f"{include_prefix}CoreWindow.h",
        f"{platform_prefix}Features.xml",
        f"{platform_prefix}Platform.xml",
        f"{platform_prefix}PreviousPlatforms.xml",
        union_metadata,
        facade_metadata,
    }

    expected_files = [destination / member for member in required_members]
    expected_files.append(
        destination
        / references_prefix
        / "Windows.Foundation.UniversalApiContract.winmd"
    )
    if all(path.is_file() for path in expected_files):
        return

    with zipfile.ZipFile(package) as archive:
        available = set(archive.namelist())
        missing = required_members - available
        if missing:
            raise RuntimeError(f"Windows SDK package is missing: {sorted(missing)}")

        members = sorted(
            member
            for member in available
            if member in required_members
            or (
                member.startswith(references_prefix)
                and member.lower().endswith(".winmd")
            )
        )
        for member in members:
            target = destination / member
            if destination.resolve() not in target.resolve().parents:
                raise RuntimeError(f"unsafe archive member: {member}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, target.open("wb") as output_stream:
                shutil.copyfileobj(source, output_stream)


def extract_zip_member(package: Path, member: str, destination: Path) -> Path:
    if destination.is_file():
        return destination

    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(package) as archive:
        if member not in archive.namelist():
            raise RuntimeError(f"{package} is missing required member: {member}")
        partial = destination.with_name(f"{destination.name}.partial")
        with archive.open(member) as source, partial.open("wb") as output_stream:
            shutil.copyfileobj(source, output_stream)
        partial.replace(destination)
    return destination


def wine_path(path: Path) -> str:
    return "Z:" + str(path.resolve()).replace("/", "\\")


def git_commit(checkout: Path) -> str:
    return output(["git", "-C", str(checkout), "rev-parse", "HEAD"])


def ensure_checkout(pin: dict[str, Any], destination: Path, *, full_history: bool = False) -> Path:
    if (destination / ".git").is_dir():
        actual = git_commit(destination)
        if actual != pin["commit"]:
            raise RuntimeError(
                f"existing checkout {destination} is {actual}; expected {pin['commit']}"
            )
        return destination
    if destination.exists():
        raise RuntimeError(f"refusing to replace non-Git path: {destination}")

    if full_history:
        run(["git", "clone", pin["repository"], str(destination)])
    else:
        destination.mkdir(parents=True)
        run(["git", "init", str(destination)])
        run(["git", "-C", str(destination), "remote", "add", "origin", pin["repository"]])
        run(
            [
                "git",
                "-C",
                str(destination),
                "fetch",
                "--depth",
                "1",
                "origin",
                pin["commit"],
            ]
        )
    run(["git", "-C", str(destination), "checkout", "--detach", pin["commit"]])
    return destination


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("/tmp/openterminal-mingw"),
        help="temporary source/build root (must be below /tmp)",
    )
    args = parser.parse_args()

    for tool in (
        "cmake",
        "clang++",
        "clang",
        "ctest",
        "git",
        "ninja",
        "wine",
        "x86_64-w64-mingw32-g++",
        "x86_64-w64-mingw32-objcopy",
        "x86_64-w64-mingw32-windres",
    ):
        require_tool(tool)

    pins = json.loads(PINS_FILE.read_text(encoding="utf-8"))
    root = ensure_tmp_root(args.root)

    terminal = ensure_checkout(pins["windows_terminal"], root / "windows-terminal")
    wil = ensure_checkout(pins["wil"], root / "wil")
    cppwinrt = ensure_checkout(pins["cppwinrt"], root / "cppwinrt")
    vcpkg = ensure_checkout(pins["vcpkg"], root / "vcpkg", full_history=True)

    run([str(vcpkg / "bootstrap-vcpkg.sh"), "-disableMetrics"], cwd=vcpkg)

    installed = root / "installed"
    dependency_env = os.environ.copy()
    dependency_env["VCPKG_DOWNLOADS"] = str(root / "downloads")
    run(
        [
            str(vcpkg / "vcpkg"),
            "install",
            "--triplet",
            "x64-mingw-static",
            f"--x-manifest-root={PHASE2_DIR}",
            f"--x-install-root={installed}",
            f"--overlay-ports={terminal / 'dep' / 'vcpkg-overlay-ports'}",
            "--disable-metrics",
        ],
        env=dependency_env,
    )

    toolchain = PHASE2_DIR / "toolchains" / "x64-mingw-static.cmake"
    cppwinrt_build = root / "cppwinrt-build"
    run(
        [
            "cmake",
            "-S",
            str(cppwinrt),
            "-B",
            str(cppwinrt_build),
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={root / 'cppwinrt-installed'}",
            f"-DCPPWINRT_BUILD_VERSION={pins['cppwinrt']['ref']}",
            "-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++",
        ]
    )
    run(["cmake", "--build", str(cppwinrt_build), "--parallel"])
    run(["cmake", "--install", str(cppwinrt_build)])

    sdk_pin = pins["windows_sdk_cpp"]
    sdk_root = root / "windows-sdk-cpp"
    sdk_package = download_checked(
        sdk_pin["url"],
        sdk_root / f"{sdk_pin['id'].lower()}.{sdk_pin['version']}.nupkg",
        sdk_pin["sha256"],
    )
    extract_windows_sdk(sdk_package, sdk_root / "extracted", sdk_pin["platform_version"])

    sdk_base = sdk_root / "extracted" / "c"
    sdk_bin = sdk_base / "bin" / sdk_pin["platform_version"] / "x64"
    sdk_include = sdk_base / "Include" / sdk_pin["platform_version"] / "winrt"
    sdk_references = sdk_base / "References" / sdk_pin["platform_version"]
    sdk_union_metadata = (
        sdk_base / "UnionMetadata" / sdk_pin["platform_version"] / "Windows.winmd"
    )
    cppwinrt_executable = root / "cppwinrt-installed" / "bin" / "cppwinrt.exe"
    wine_env = os.environ.copy()
    wine_env["WINEDEBUG"] = "-all"
    wine_env["WINEPATH"] = wine_path(sdk_bin)

    sdk_projection = root / "cppwinrt-sdk"
    sdk_projection_header = sdk_projection / "winrt" / "Windows.Foundation.h"
    if not sdk_projection_header.is_file():
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(sdk_references),
                "-output",
                wine_path(sdk_projection),
            ],
            env=wine_env,
        )
    if not sdk_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the Windows SDK projection")

    winui_metadata = root / "winui-metadata"
    for pin_name, output_name in (
        ("microsoft_ui_xaml_package", "Microsoft.UI.Xaml.winmd"),
        ("microsoft_web_webview2_package", "Microsoft.Web.WebView2.Core.winmd"),
    ):
        package_pin = pins[pin_name]
        package = download_checked(
            package_pin["url"],
            root
            / "winui-packages"
            / f"{package_pin['id'].lower()}.{package_pin['version']}.nupkg",
            package_pin["sha256"],
        )
        extract_zip_member(
            package,
            package_pin["winmd_path"],
            winui_metadata / output_name,
        )

    winui_projection = root / "cppwinrt-winui"
    winui_projection_header = (
        winui_projection / "winrt" / "Microsoft.UI.Xaml.Controls.h"
    )
    webview_projection_header = (
        winui_projection / "winrt" / "Microsoft.Web.WebView2.Core.h"
    )
    if not (winui_projection_header.is_file() and webview_projection_header.is_file()):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(winui_metadata),
                "-reference",
                wine_path(sdk_references),
                "-output",
                wine_path(winui_projection),
            ],
            env=wine_env,
        )
    if not winui_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the WinUI 2 projection")
    if not webview_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the WebView2 projection")

    terminalcore_metadata_dir = root / "terminalcore-winmd"
    terminalcore_metadata = terminalcore_metadata_dir / "Microsoft.Terminal.Core.winmd"
    if not terminalcore_metadata.is_file():
        terminalcore_metadata_dir.mkdir(parents=True, exist_ok=True)
        run(
            [
                "wine",
                str(sdk_bin / "midlrt.exe"),
                "/nologo",
                "/winrt",
                "/nomidl",
                "/no_cpp",
                "/I",
                wine_path(sdk_include),
                "/metadata_dir",
                wine_path(sdk_references),
                "/reference",
                wine_path(sdk_union_metadata),
                "/out",
                wine_path(terminalcore_metadata_dir),
                "/winmd",
                terminalcore_metadata.name,
                wine_path(terminal / "src" / "cascadia" / "TerminalCore" / "ICoreSettings.idl"),
            ],
            env=wine_env,
        )
    if not terminalcore_metadata.is_file():
        raise RuntimeError("MIDLRT did not generate Microsoft.Terminal.Core.winmd")

    terminalcore_projection = root / "cppwinrt-terminalcore"
    terminalcore_projection_header = (
        terminalcore_projection / "winrt" / "Microsoft.Terminal.Core.h"
    )
    if not terminalcore_projection_header.is_file():
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(terminalcore_metadata),
                "-reference",
                wine_path(sdk_references),
                "-output",
                wine_path(terminalcore_projection),
            ],
            env=wine_env,
        )
    if not terminalcore_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the TerminalCore projection")

    terminalconnection_dir = terminal / "src" / "cascadia" / "TerminalConnection"
    terminalconnection_metadata_dir = root / "terminalconnection-winmd"
    terminalconnection_metadata_dir.mkdir(parents=True, exist_ok=True)
    terminalconnection_idls = (
        "ITerminalConnection",
        "ConnectionInformation",
        "ConptyConnection",
        "EchoConnection",
        "AzureConnection",
    )
    for idl_name in terminalconnection_idls:
        metadata = (
            terminalconnection_metadata_dir
            / f"Microsoft.Terminal.TerminalConnection.{idl_name}.winmd"
        )
        if not metadata.is_file():
            run(
                [
                    "wine",
                    str(sdk_bin / "midlrt.exe"),
                    "/nologo",
                    "/winrt",
                    "/nomidl",
                    "/no_cpp",
                    "/I",
                    wine_path(sdk_include),
                    "/I",
                    wine_path(terminalconnection_dir),
                    "/metadata_dir",
                    wine_path(sdk_references),
                    "/reference",
                    wine_path(sdk_union_metadata),
                    "/out",
                    wine_path(terminalconnection_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(terminalconnection_dir / f"{idl_name}.idl"),
                ],
                env=wine_env,
            )
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    terminalconnection_projection = root / "cppwinrt-terminalconnection"
    terminalconnection_projection_header = (
        terminalconnection_projection
        / "winrt"
        / "Microsoft.Terminal.TerminalConnection.h"
    )
    terminalconnection_echo_glue = (
        terminalconnection_projection
        / "Microsoft"
        / "Terminal"
        / "TerminalConnection"
        / "EchoConnection.g.cpp"
    )
    if not (
        terminalconnection_projection_header.is_file()
        and terminalconnection_echo_glue.is_file()
    ):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(terminalconnection_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-output",
                wine_path(terminalconnection_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not terminalconnection_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the TerminalConnection projection")
    if not terminalconnection_echo_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate the TerminalConnection component glue")

    uihelpers_dir = terminal / "src" / "cascadia" / "UIHelpers"
    uihelpers_metadata_dir = root / "uihelpers-winmd"
    uihelpers_metadata_dir.mkdir(parents=True, exist_ok=True)
    uihelpers_idls = (
        "Converters",
        "IDirectKeyListener",
        "IconPathConverter",
        "ResourceString",
        "TextMenuFlyout",
    )
    for idl_name in uihelpers_idls:
        metadata = uihelpers_metadata_dir / f"Microsoft.Terminal.UI.{idl_name}.winmd"
        if not metadata.is_file():
            run(
                [
                    "wine",
                    str(sdk_bin / "midlrt.exe"),
                    "/nologo",
                    "/winrt",
                    "/nomidl",
                    "/no_cpp",
                    "/I",
                    wine_path(sdk_include),
                    "/I",
                    wine_path(uihelpers_dir),
                    "/metadata_dir",
                    wine_path(sdk_references),
                    "/reference",
                    wine_path(sdk_union_metadata),
                    "/reference",
                    wine_path(winui_metadata / "Microsoft.UI.Xaml.winmd"),
                    "/out",
                    wine_path(uihelpers_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(uihelpers_dir / f"{idl_name}.idl"),
                ],
                env=wine_env,
            )
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    uihelpers_projection = root / "cppwinrt-uihelpers"
    uihelpers_projection_header = (
        uihelpers_projection / "winrt" / "Microsoft.Terminal.UI.h"
    )
    uihelpers_converters_glue = (
        uihelpers_projection
        / "Microsoft"
        / "Terminal"
        / "UI"
        / "Converters.g.cpp"
    )
    if not (uihelpers_projection_header.is_file() and uihelpers_converters_glue.is_file()):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(uihelpers_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-reference",
                wine_path(winui_metadata),
                "-output",
                wine_path(uihelpers_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not uihelpers_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the UIHelpers projection")
    if not uihelpers_converters_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate the UIHelpers component glue")

    terminalcontrol_dir = terminal / "src" / "cascadia" / "TerminalControl"
    terminalcontrol_metadata_dir = root / "terminalcontrol-winmd"
    terminalcontrol_metadata_dir.mkdir(parents=True, exist_ok=True)
    terminalcontrol_idls = (
        "ControlCore",
        "ControlInteractivity",
        "EventArgs",
        "IControlAppearance",
        "IControlSettings",
        "ICoreState",
        "IKeyBindings",
        "IMouseWheelListener",
        "InteractivityAutomationPeer",
        "KeyChord",
        "ScrollBarVisualStateManager",
        "SearchBoxControl",
        "TermControl",
        "TermControlAutomationPeer",
        "XamlLights",
    )
    terminalconnection_references = sorted(terminalconnection_metadata_dir.glob("*.winmd"))
    for idl_name in terminalcontrol_idls:
        metadata = (
            terminalcontrol_metadata_dir
            / f"Microsoft.Terminal.Control.{idl_name}.winmd"
        )
        if not metadata.is_file():
            command = [
                "wine",
                str(sdk_bin / "midlrt.exe"),
                "/nologo",
                "/winrt",
                "/nomidl",
                "/no_cpp",
                "/I",
                wine_path(sdk_include),
                "/I",
                wine_path(terminalcontrol_dir),
                "/metadata_dir",
                wine_path(sdk_references),
                "/reference",
                wine_path(sdk_union_metadata),
                "/reference",
                wine_path(terminalcore_metadata),
                "/reference",
                wine_path(winui_metadata / "Microsoft.UI.Xaml.winmd"),
            ]
            for reference in terminalconnection_references:
                command.extend(("/reference", wine_path(reference)))
            command.extend(
                (
                    "/out",
                    wine_path(terminalcontrol_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(terminalcontrol_dir / f"{idl_name}.idl"),
                )
            )
            run(command, env=wine_env)
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    terminalcontrol_projection = root / "cppwinrt-terminalcontrol"
    terminalcontrol_projection_header = (
        terminalcontrol_projection / "winrt" / "Microsoft.Terminal.Control.h"
    )
    terminalcontrol_core_glue = (
        terminalcontrol_projection
        / "Microsoft"
        / "Terminal"
        / "Control"
        / "ControlCore.g.cpp"
    )
    if not (
        terminalcontrol_projection_header.is_file()
        and terminalcontrol_core_glue.is_file()
    ):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(terminalcontrol_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-reference",
                wine_path(winui_metadata),
                "-reference",
                wine_path(terminalcore_metadata_dir),
                "-reference",
                wine_path(terminalconnection_metadata_dir),
                "-reference",
                wine_path(uihelpers_metadata_dir),
                "-output",
                wine_path(terminalcontrol_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not terminalcontrol_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the TerminalControl projection")
    if not terminalcontrol_core_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate TerminalControl component glue")

    settingsmodel_dir = terminal / "src" / "cascadia" / "TerminalSettingsModel"
    settingsmodel_prepared_dir = root / "settingsmodel-idl-prepared"
    run(
        [
            sys.executable,
            str(PHASE2_DIR / "scripts" / "prepare_midl3.py"),
            "--clang",
            "clang",
            "--source",
            str(settingsmodel_dir),
            "--output",
            str(settingsmodel_prepared_dir),
        ]
    )

    settingsmodel_metadata_dir = root / "settingsmodel-winmd"
    settingsmodel_metadata_dir.mkdir(parents=True, exist_ok=True)
    terminalcontrol_references = sorted(terminalcontrol_metadata_dir.glob("*.winmd"))
    for idl in sorted(settingsmodel_prepared_dir.glob("*.idl")):
        metadata = (
            settingsmodel_metadata_dir
            / f"Microsoft.Terminal.Settings.Model.{idl.stem}.winmd"
        )
        if not metadata.is_file():
            command = [
                "wine",
                str(sdk_bin / "midlrt.exe"),
                "/nologo",
                "/winrt",
                "/nomidl",
                "/no_cpp",
                "/I",
                wine_path(sdk_include),
                "/I",
                wine_path(settingsmodel_prepared_dir),
                "/metadata_dir",
                wine_path(sdk_references),
                "/reference",
                wine_path(sdk_union_metadata),
                "/reference",
                wine_path(terminalcore_metadata),
                "/reference",
                wine_path(winui_metadata / "Microsoft.UI.Xaml.winmd"),
            ]
            for reference in terminalconnection_references + terminalcontrol_references:
                command.extend(("/reference", wine_path(reference)))
            command.extend(
                (
                    "/out",
                    wine_path(settingsmodel_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(idl),
                )
            )
            run(command, env=wine_env)
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    settingsmodel_projection = root / "cppwinrt-settingsmodel"
    settingsmodel_projection_header = (
        settingsmodel_projection
        / "winrt"
        / "Microsoft.Terminal.Settings.Model.h"
    )
    settingsmodel_action_glue = (
        settingsmodel_projection
        / "Microsoft"
        / "Terminal"
        / "Settings"
        / "Model"
        / "ActionAndArgs.g.cpp"
    )
    if not (
        settingsmodel_projection_header.is_file()
        and settingsmodel_action_glue.is_file()
    ):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(settingsmodel_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-reference",
                wine_path(winui_metadata),
                "-reference",
                wine_path(terminalcore_metadata_dir),
                "-reference",
                wine_path(terminalconnection_metadata_dir),
                "-reference",
                wine_path(terminalcontrol_metadata_dir),
                "-output",
                wine_path(settingsmodel_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not settingsmodel_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the SettingsModel projection")
    if not settingsmodel_action_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate SettingsModel component glue")

    uimarkdown_dir = terminal / "src" / "cascadia" / "UIMarkdown"
    uimarkdown_metadata_dir = root / "uimarkdown-winmd"
    uimarkdown_metadata_dir.mkdir(parents=True, exist_ok=True)
    uihelpers_references = sorted(uihelpers_metadata_dir.glob("*.winmd"))
    for idl_name in ("Builder", "CodeBlock"):
        metadata = (
            uimarkdown_metadata_dir
            / f"Microsoft.Terminal.UI.Markdown.{idl_name}.winmd"
        )
        if not metadata.is_file():
            command = [
                "wine",
                str(sdk_bin / "midlrt.exe"),
                "/nologo",
                "/winrt",
                "/nomidl",
                "/no_cpp",
                "/I",
                wine_path(sdk_include),
                "/I",
                wine_path(uimarkdown_dir),
                "/metadata_dir",
                wine_path(sdk_references),
                "/reference",
                wine_path(sdk_union_metadata),
                "/reference",
                wine_path(winui_metadata / "Microsoft.UI.Xaml.winmd"),
            ]
            for reference in uihelpers_references:
                command.extend(("/reference", wine_path(reference)))
            command.extend(
                (
                    "/out",
                    wine_path(uimarkdown_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(uimarkdown_dir / f"{idl_name}.idl"),
                )
            )
            run(command, env=wine_env)
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    uimarkdown_projection = root / "cppwinrt-uimarkdown"
    uimarkdown_projection_header = (
        uimarkdown_projection / "winrt" / "Microsoft.Terminal.UI.Markdown.h"
    )
    uimarkdown_builder_glue = (
        uimarkdown_projection
        / "Microsoft"
        / "Terminal"
        / "UI"
        / "Markdown"
        / "Builder.g.cpp"
    )
    if not (
        uimarkdown_projection_header.is_file()
        and uimarkdown_builder_glue.is_file()
    ):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(uimarkdown_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-reference",
                wine_path(winui_metadata),
                "-reference",
                wine_path(uihelpers_metadata_dir),
                "-output",
                wine_path(uimarkdown_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not uimarkdown_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the UIMarkdown projection")
    if not uimarkdown_builder_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate UIMarkdown component glue")

    settingseditor_dir = terminal / "src" / "cascadia" / "TerminalSettingsEditor"
    settingseditor_prepared_dir = root / "settingseditor-idl-prepared"
    run(
        [
            sys.executable,
            str(PHASE2_DIR / "scripts" / "prepare_midl3.py"),
            "--clang",
            "clang",
            "--source",
            str(settingseditor_dir),
            "--output",
            str(settingseditor_prepared_dir),
        ]
    )
    settingseditor_metadata_dir = root / "settingseditor-winmd"
    settingseditor_metadata_dir.mkdir(parents=True, exist_ok=True)
    settingseditor_references = sorted(
        terminalconnection_metadata_dir.glob("*.winmd")
    ) + sorted(terminalcontrol_metadata_dir.glob("*.winmd"))
    settingseditor_references += sorted(settingsmodel_metadata_dir.glob("*.winmd"))
    for idl in sorted(settingseditor_prepared_dir.glob("*.idl")):
        metadata = (
            settingseditor_metadata_dir
            / f"Microsoft.Terminal.Settings.Editor.{idl.stem}.winmd"
        )
        if not metadata.is_file():
            command = [
                "wine",
                str(sdk_bin / "midlrt.exe"),
                "/nologo",
                "/winrt",
                "/nomidl",
                "/no_cpp",
                "/I",
                wine_path(sdk_include),
                "/I",
                wine_path(settingseditor_prepared_dir),
                "/metadata_dir",
                wine_path(sdk_references),
                "/reference",
                wine_path(sdk_union_metadata),
                "/reference",
                wine_path(terminalcore_metadata),
                "/reference",
                wine_path(winui_metadata / "Microsoft.UI.Xaml.winmd"),
            ]
            for reference in uihelpers_references + settingseditor_references:
                command.extend(("/reference", wine_path(reference)))
            command.extend(
                (
                    "/out",
                    wine_path(settingseditor_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(idl),
                )
            )
            run(command, env=wine_env)
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    settingseditor_projection = root / "cppwinrt-settingseditor"
    settingseditor_projection_header = (
        settingseditor_projection
        / "winrt"
        / "Microsoft.Terminal.Settings.Editor.h"
    )
    settingseditor_actions_glue = (
        settingseditor_projection
        / "Microsoft"
        / "Terminal"
        / "Settings"
        / "Editor"
        / "Actions.g.cpp"
    )
    if not (
        settingseditor_projection_header.is_file()
        and settingseditor_actions_glue.is_file()
    ):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(settingseditor_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-reference",
                wine_path(winui_metadata),
                "-reference",
                wine_path(uihelpers_metadata_dir),
                "-reference",
                wine_path(terminalcore_metadata_dir),
                "-reference",
                wine_path(terminalconnection_metadata_dir),
                "-reference",
                wine_path(terminalcontrol_metadata_dir),
                "-reference",
                wine_path(settingsmodel_metadata_dir),
                "-output",
                wine_path(settingseditor_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not settingseditor_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the SettingsEditor projection")
    if not settingseditor_actions_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate SettingsEditor component glue")

    winget_pin = pins["microsoft_windows_package_manager_cominterop_package"]
    winget_package = download_checked(
        winget_pin["url"],
        root
        / "support-packages"
        / f"{winget_pin['id'].lower()}.{winget_pin['version']}.nupkg",
        winget_pin["sha256"],
    )
    winget_metadata = extract_zip_member(
        winget_package,
        winget_pin["winmd_path"],
        root / "winget-metadata" / "Microsoft.Management.Deployment.winmd",
    )
    winget_projection = root / "cppwinrt-winget"
    winget_projection_header = (
        winget_projection / "winrt" / "Microsoft.Management.Deployment.h"
    )
    if not winget_projection_header.is_file():
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(winget_metadata),
                "-reference",
                wine_path(sdk_references),
                "-output",
                wine_path(winget_projection),
            ],
            env=wine_env,
        )
    if not winget_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the WinGet projection")

    terminalapp_dir = terminal / "src" / "cascadia" / "TerminalApp"
    terminalapp_prepared_dir = root / "terminalapp-idl-prepared"
    run(
        [
            sys.executable,
            str(PHASE2_DIR / "scripts" / "prepare_midl3.py"),
            "--clang",
            "clang",
            "--source",
            str(terminalapp_dir),
            "--output",
            str(terminalapp_prepared_dir),
        ]
    )
    terminalapp_metadata_dir = root / "terminalapp-winmd"
    terminalapp_metadata_dir.mkdir(parents=True, exist_ok=True)
    terminalapp_references = (
        uihelpers_references
        + sorted(terminalconnection_metadata_dir.glob("*.winmd"))
        + sorted(terminalcontrol_metadata_dir.glob("*.winmd"))
        + sorted(settingsmodel_metadata_dir.glob("*.winmd"))
        + sorted(settingseditor_metadata_dir.glob("*.winmd"))
        + sorted(uimarkdown_metadata_dir.glob("*.winmd"))
    )
    for idl in sorted(terminalapp_prepared_dir.glob("*.idl")):
        metadata = terminalapp_metadata_dir / f"TerminalApp.{idl.stem}.winmd"
        if not metadata.is_file():
            command = [
                "wine",
                str(sdk_bin / "midlrt.exe"),
                "/nologo",
                "/winrt",
                "/nomidl",
                "/no_cpp",
                "/I",
                wine_path(sdk_include),
                "/I",
                wine_path(terminalapp_prepared_dir),
                "/metadata_dir",
                wine_path(sdk_references),
                "/reference",
                wine_path(sdk_union_metadata),
                "/reference",
                wine_path(terminalcore_metadata),
                "/reference",
                wine_path(winui_metadata / "Microsoft.UI.Xaml.winmd"),
            ]
            for reference in terminalapp_references:
                command.extend(("/reference", wine_path(reference)))
            command.extend(
                (
                    "/out",
                    wine_path(terminalapp_metadata_dir),
                    "/winmd",
                    metadata.name,
                    wine_path(idl),
                )
            )
            run(command, env=wine_env)
        if not metadata.is_file():
            raise RuntimeError(f"MIDLRT did not generate {metadata.name}")

    terminalapp_projection = root / "cppwinrt-terminalapp"
    terminalapp_projection_header = terminalapp_projection / "winrt" / "TerminalApp.h"
    terminalapp_app_glue = terminalapp_projection / "TerminalApp" / "App.g.cpp"
    if not (terminalapp_projection_header.is_file() and terminalapp_app_glue.is_file()):
        run(
            [
                "wine",
                str(cppwinrt_executable),
                "-input",
                wine_path(terminalapp_metadata_dir),
                "-reference",
                wine_path(sdk_references),
                "-reference",
                wine_path(winui_metadata),
                "-reference",
                wine_path(uihelpers_metadata_dir),
                "-reference",
                wine_path(terminalcore_metadata_dir),
                "-reference",
                wine_path(terminalconnection_metadata_dir),
                "-reference",
                wine_path(terminalcontrol_metadata_dir),
                "-reference",
                wine_path(settingsmodel_metadata_dir),
                "-reference",
                wine_path(settingseditor_metadata_dir),
                "-reference",
                wine_path(uimarkdown_metadata_dir),
                "-output",
                wine_path(terminalapp_projection),
                "-component",
                "-optimize",
            ],
            env=wine_env,
        )
    if not terminalapp_projection_header.is_file():
        raise RuntimeError("C++/WinRT did not generate the TerminalApp projection")
    if not terminalapp_app_glue.is_file():
        raise RuntimeError("C++/WinRT did not generate TerminalApp component glue")

    vs_setup_pin = pins["microsoft_visualstudio_setup_configuration_native_package"]
    vs_setup_package = download_checked(
        vs_setup_pin["url"],
        root
        / "support-packages"
        / f"{vs_setup_pin['id'].lower()}.{vs_setup_pin['version']}.nupkg",
        vs_setup_pin["sha256"],
    )
    vs_setup_header = extract_zip_member(
        vs_setup_package,
        vs_setup_pin["header_path"],
        root / "visualstudio-setup" / "include" / "Setup.Configuration.h",
    )

    parser_build = root / "native-build"
    run(
        [
            "cmake",
            "-S",
            str(PHASE2_DIR),
            "-B",
            str(parser_build),
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DCMAKE_PREFIX_PATH={installed / 'x64-mingw-static'}",
            f"-DTERMINAL_SOURCE_DIR={terminal}",
            f"-DWIL_SOURCE_DIR={wil}",
            f"-DCPPWINRT_SDK_INCLUDE_DIR={sdk_projection}",
            f"-DCPPWINRT_TERMINALCORE_INCLUDE_DIR={terminalcore_projection}",
            f"-DCPPWINRT_TERMINALCONNECTION_INCLUDE_DIR={terminalconnection_projection}",
            f"-DCPPWINRT_WINUI_INCLUDE_DIR={winui_projection}",
            f"-DCPPWINRT_UIHELPERS_INCLUDE_DIR={uihelpers_projection}",
            f"-DCPPWINRT_TERMINALCONTROL_INCLUDE_DIR={terminalcontrol_projection}",
            f"-DCPPWINRT_SETTINGSMODEL_INCLUDE_DIR={settingsmodel_projection}",
            f"-DCPPWINRT_UIMARKDOWN_INCLUDE_DIR={uimarkdown_projection}",
            f"-DCPPWINRT_SETTINGSEDITOR_INCLUDE_DIR={settingseditor_projection}",
            f"-DCPPWINRT_TERMINALAPP_INCLUDE_DIR={terminalapp_projection}",
            f"-DCPPWINRT_WINGET_INCLUDE_DIR={winget_projection}",
            f"-DVISUALSTUDIO_SETUP_INCLUDE_DIR={vs_setup_header.parent}",
            f"-DWINDOWS_SDK_BIN_DIR={sdk_bin}",
            f"-DWINDOWS_SDK_WINRT_INCLUDE_DIR={sdk_include}",
            "-DCMAKE_BUILD_TYPE=Release",
        ]
    )
    run(["cmake", "--build", str(parser_build), "--parallel"])
    run(["ctest", "--test-dir", str(parser_build), "--output-on-failure"])

    print(f"MinGW native-library build completed under {root}")


if __name__ == "__main__":
    main()
