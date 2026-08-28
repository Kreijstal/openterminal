#!/usr/bin/env python3
"""Harvest the pinned SDK XAML compiler and finish TerminalControl with MinGW.

The Windows SDK tools, merged metadata, generated headers, XBF files, and Wine
prefix all remain below /tmp. Run build_mingw.py first so its pinned source,
metadata, C++/WinRT projections, and CMake cache are available.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from xml.sax.saxutils import quoteattr

import build_mingw as common


PHASE2_DIR = Path(__file__).resolve().parents[1]
PINS_FILE = PHASE2_DIR / "upstreams.json"

TERMINAL_CONNECTION_IDLS = (
    "ITerminalConnection",
    "ConnectionInformation",
    "ConptyConnection",
    "EchoConnection",
    "AzureConnection",
)
UI_HELPERS_IDLS = (
    "Converters",
    "IDirectKeyListener",
    "IconPathConverter",
    "ResourceString",
    "TextMenuFlyout",
)
TERMINAL_CONTROL_IDLS = (
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
SETTINGS_EDITOR_PAGES = (
    "Actions",
    "EditAction",
    "AddProfile",
    "CommonResources",
    "GlobalAppearance",
    "ColorSchemes",
    "NullableColorPicker",
    "IconPicker",
    "EditColorScheme",
    "Interaction",
    "Compatibility",
    "KeyChordListener",
    "Launch",
    "NewTabMenu",
    "MainPage",
    "Extensions",
    "Profiles_Base",
    "Profiles_Base_Orphaned",
    "Profiles_Advanced",
    "Profiles_Appearance",
    "Profiles_Terminal",
    "Appearances",
    "Rendering",
    "SettingContainerStyle",
)
SETTINGS_EDITOR_CLASS_PAGES = tuple(
    page
    for page in SETTINGS_EDITOR_PAGES
    if page not in {"CommonResources", "SettingContainerStyle"}
)
TERMINAL_APP_PAGES = (
    "App",
    "AboutDialog",
    "MinMaxCloseControl",
    "TerminalPage",
    "TitlebarControl",
    "TabRowControl",
    "TabHeaderControl",
    "HighlightedTextControlStyle",
    "ColorPickupFlyout",
    "CommandPalette",
    "SuggestionsControl",
    "SnippetsPaneContent",
    "MarkdownPaneContent",
)
TERMINAL_APP_CLASS_PAGES = tuple(
    page for page in TERMINAL_APP_PAGES if page != "HighlightedTextControlStyle"
)

WINRT_INCLUDE_CASE = {
    "winrt/windows.foundation.h": "winrt/Windows.Foundation.h",
    "winrt/windows.ui.xaml.controls.h": "winrt/Windows.UI.Xaml.Controls.h",
    "winrt/windows.ui.xaml.data.h": "winrt/Windows.UI.Xaml.Data.h",
    "winrt/windows.ui.xaml.interop.h": "winrt/Windows.UI.Xaml.Interop.h",
    "winrt/windows.ui.xaml.markup.h": "winrt/Windows.UI.Xaml.Markup.h",
}


def run(
    arguments: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    print(f"+ {shlex.join(arguments)}", flush=True)
    subprocess.run(arguments, cwd=cwd, env=env, check=True)


def require_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise RuntimeError(f"missing {description}: {path}")
    return path


def require_tmp_path(path: Path, description: str) -> Path:
    resolved = path.resolve()
    tmp = Path("/tmp").resolve()
    if resolved == tmp or tmp not in resolved.parents:
        raise RuntimeError(f"{description} must be a child of /tmp: {resolved}")
    return resolved


def win_path(path: Path, *, directory: bool = False) -> str:
    value = common.wine_path(path)
    if directory and not value.endswith("\\"):
        value += "\\"
    return value


def merge_metadata(
    mdmerge: Path,
    inputs: list[Path],
    metadata_dirs: list[Path],
    output_dir: Path,
    output_name: str,
    wine_env: dict[str, str],
    namespace_depth: int = 3,
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / output_name
    output.unlink(missing_ok=True)

    command = ["wine", str(mdmerge)]
    for input_path in inputs:
        command.extend(("-i", str(require_file(input_path, "metadata input"))))
    for metadata_dir in metadata_dirs:
        if not metadata_dir.is_dir():
            raise RuntimeError(f"missing metadata reference directory: {metadata_dir}")
        command.extend(("-metadata_dir", str(metadata_dir)))
    command.extend(
        ("-o", str(output_dir), "-n", str(namespace_depth), "-partial", "-v")
    )
    run(command, env=wine_env)
    return require_file(output, "merged metadata")


def xml_attributes(attributes: dict[str, str], indent: str = "        ") -> str:
    return ("\n" + indent).join(
        f"{name}={quoteattr(value)}" for name, value in attributes.items()
    )


def compile_xaml_element(
    *,
    pass1: bool,
    output_dir: Path,
    sdk_base: Path,
    platform_version: str,
    references: list[Path],
    local_assembly: Path | None,
    include_dirs: list[Path],
    root_namespace: str,
    pri_index_name: str,
    project_name: str,
    language: str,
    use_cppwinrt_local_assembly: bool,
    code_generation_control_flags: str,
) -> str:
    reference_paths = [path.parent for path in references]
    reference_paths.append(Path(r"C:\windows\Microsoft.NET\Framework64\v4.0.30319"))
    attributes = {
        "LanguageSourceExtension": ".cpp",
        "Language": language,
        "RootNamespace": root_namespace,
        "XamlPages": "@(Page)",
        "PriIndexName": pri_index_name,
        "ProjectName": project_name,
        "IsPass1": "True" if pass1 else "False",
        "ProjectPath": "$(MSBuildProjectFullPath)",
        "OutputPath": win_path(output_dir, directory=True),
        "OutputType": "library",
        "ReferenceAssemblies": "@(ReferencePath)",
        "ReferenceAssemblyPaths": ";".join(
            str(path) if str(path).startswith("C:\\") else win_path(path)
            for path in reference_paths
        ),
        "ForceSharedStateShutdown": "False",
        "CompileMode": "RealBuildPass1" if pass1 else "RealBuildPass2",
        "XAMLFingerprint": "False",
        "SavedStateFile": win_path(output_dir / "XamlSaveStateFile.xml"),
        "TargetPlatformMinVersion": "10.0.18362.0",
        "WindowsSdkPath": win_path(sdk_base, directory=True),
        "XamlComponentResourceLocation": "nested",
        "EnableTypeInfoReflection": "False",
        "EnableXBindDiagnostics": "False",
        "EnableDefaultValidationContextGeneration": "True",
    }
    if local_assembly is not None and use_cppwinrt_local_assembly:
        attributes["CppWinRTLocalAssembly"] = "@(XamlCppWinRTLocalAssembly)"
    if code_generation_control_flags:
        attributes["CodeGenerationControlFlags"] = code_generation_control_flags
    if not pass1:
        attributes.update(
            {
                "DisableXbfGeneration": "False",
                "DisableXbfLineInfo": "False",
                "ClIncludeFiles": "@(ClInclude)",
                "CIncludeDirectories": ";".join(win_path(path) for path in include_dirs),
                "PlatformXmlDir": win_path(
                    sdk_base / "Platforms" / "UAP" / platform_version,
                    directory=True,
                ),
            }
        )
        if local_assembly is not None:
            attributes["LocalAssembly"] = win_path(local_assembly)

    prefix = "Pass1" if pass1 else "Pass2"
    outputs = [
        f'<Output ItemName="{prefix}GeneratedCode" TaskParameter="GeneratedCodeFiles" />',
        f'<Output ItemName="{prefix}GeneratedXaml" TaskParameter="GeneratedXamlFiles" />',
    ]
    if not pass1:
        outputs.extend(
            (
                '<Output ItemName="Pass2GeneratedXbf" TaskParameter="GeneratedXbfFiles" />',
                '<Output ItemName="Pass2GeneratedXamlPages" TaskParameter="GeneratedXamlPagesFiles" />',
            )
        )
    return (
        f"    <CompileXaml\n        {xml_attributes(attributes)}>\n"
        + "\n".join(f"      {line}" for line in outputs)
        + "\n    </CompileXaml>"
    )


def write_msbuild_project(
    project: Path,
    task_dll: Path,
    pages: list[Path],
    headers: list[Path],
    references: list[Path],
    local_assembly: Path | None,
    output_dir: Path,
    sdk_base: Path,
    platform_version: str,
    include_dirs: list[Path],
    root_namespace: str,
    pri_index_name: str,
    project_name: str,
    language: str = "CppWinRT",
    use_cppwinrt_local_assembly: bool = True,
    code_generation_control_flags: str = "",
) -> None:
    page_items_list: list[str] = []
    for page in pages:
        metadata = f"<Link>{page.name}</Link>"
        if "x:Class=" not in page.read_text(encoding="utf-8-sig"):
            # The SDK task otherwise tries to resolve a null C++/WinRT class
            # name while probing standalone ResourceDictionary pages.
            metadata += "<Type>DefaultStyle</Type>"
        page_items_list.append(
            f"    <Page Include={quoteattr(win_path(page))}>{metadata}</Page>"
        )
    page_items = "\n".join(page_items_list)
    header_items = "\n".join(
        f"    <ClInclude Include={quoteattr(win_path(header))} />" for header in headers
    )
    reference_items = "\n".join(
        f"    <ReferencePath Include={quoteattr(win_path(reference))} />"
        for reference in references
    )
    pass1 = compile_xaml_element(
        pass1=True,
        output_dir=output_dir,
        sdk_base=sdk_base,
        platform_version=platform_version,
        references=references,
        local_assembly=local_assembly,
        include_dirs=include_dirs,
        root_namespace=root_namespace,
        pri_index_name=pri_index_name,
        project_name=project_name,
        language=language,
        use_cppwinrt_local_assembly=use_cppwinrt_local_assembly,
        code_generation_control_flags=code_generation_control_flags,
    )
    pass2 = compile_xaml_element(
        pass1=False,
        output_dir=output_dir,
        sdk_base=sdk_base,
        platform_version=platform_version,
        references=references,
        local_assembly=local_assembly,
        include_dirs=include_dirs,
        root_namespace=root_namespace,
        pri_index_name=pri_index_name,
        project_name=project_name,
        language=language,
        use_cppwinrt_local_assembly=use_cppwinrt_local_assembly,
        code_generation_control_flags=code_generation_control_flags,
    )
    content = f"""<Project DefaultTargets="Pass2" ToolsVersion="4.0"
         xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <UsingTask TaskName="Microsoft.Windows.UI.Xaml.Build.Tasks.CompileXaml"
             AssemblyFile={quoteattr(win_path(task_dll))} />
  <ItemGroup>
{page_items}
{header_items}
{reference_items}
{f'    <XamlCppWinRTLocalAssembly Include={quoteattr(win_path(local_assembly))} />' if local_assembly is not None and use_cppwinrt_local_assembly else ''}
  </ItemGroup>
  <Target Name="Pass1">
    <MakeDir Directories={quoteattr(win_path(output_dir))} />
{pass1}
    <Message Text="Generated code: @(Pass1GeneratedCode)" Importance="high" />
  </Target>
  <Target Name="Pass2">
{pass2}
    <Message Text="Generated XBF: @(Pass2GeneratedXbf)" Importance="high" />
  </Target>
</Project>
"""
    project.write_text(content, encoding="utf-8", newline="\n")


def normalized_generated_copy(source: Path, destination: Path) -> None:
    content = source.read_text(encoding="utf-8-sig")
    for lower_name, canonical_name in WINRT_INCLUDE_CASE.items():
        content = content.replace(lower_name, canonical_name)
    if "#include <winrt/windows." in content:
        raise RuntimeError(f"unhandled lowercase C++/WinRT include in {source}")
    system_load = (
        "Application::LoadComponent(*this, resourceLocator, "
        "ComponentResourceLocation::Nested);"
    )
    open_load = """const auto implementation = static_cast<D*>(this)->get_strong();
            const auto component = implementation.template as<
                winrt::Windows::Foundation::IInspectable>();
            const auto absolute = resourceLocator.AbsoluteUri();
            winrt::check_hresult(OpenXamlLoadComponent(
                reinterpret_cast<::IInspectable*>(winrt::get_abi(component)),
                reinterpret_cast<::HSTRING>(winrt::get_abi(absolute))));"""
    content = content.replace(system_load, open_load)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and destination.read_text(encoding="utf-8") == content:
        return
    destination.write_text(content, encoding="utf-8", newline="\n")


def normalized_metadata_provider_copy(
    source: Path, destination: Path, namespace: str
) -> None:
    content = source.read_text(encoding="utf-8-sig")
    content = content.replace('#include "XamlMetaDataProvider.g.h"\n', "")
    old_base = (
        f"struct XamlMetaDataProvider : public ::winrt::{namespace}::implementation::"
        "XamlMetaDataProvider_base<XamlMetaDataProvider>"
    )
    new_base = (
        "struct XamlMetaDataProvider : public ::winrt::implements<"
        "XamlMetaDataProvider, "
        "::winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider>"
    )
    if content.count(old_base) != 1:
        raise RuntimeError(f"expected generated {namespace} metadata-provider base")
    content = content.replace(old_base, new_base)
    factory_namespace = f"namespace winrt::{namespace}::factory_implementation"
    factory_start = content.find(factory_namespace)
    if factory_start < 0:
        raise RuntimeError(f"expected generated {namespace} metadata-provider factory")
    content = content[:factory_start].rstrip() + "\n"
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and destination.read_text(encoding="utf-8") == content:
        return
    destination.write_text(content, encoding="utf-8", newline="\n")


def normalized_xaml_type_info_copy(
    source: Path, destination: Path, namespace: str
) -> None:
    normalized_generated_copy(source, destination)
    content = destination.read_text(encoding="utf-8")
    match = re.search(
        r'__declspec\(uuid\("([0-9A-Fa-f-]+)"\)\) IXamlUserType', content
    )
    if not match:
        raise RuntimeError(f"expected generated {namespace} IXamlUserType UUID")
    uuid = match.group(1).replace("-", "")
    fields = [
        f"0x{uuid[0:8]}",
        f"0x{uuid[8:12]}",
        f"0x{uuid[12:16]}",
        *(f"0x{uuid[index:index + 2]}" for index in range(16, 32, 2)),
    ]
    declaration = (
        "\n#ifdef __MINGW32__\n"
        f"__CRT_UUID_DECL(winrt::{namespace}::implementation::IXamlUserType, "
        + ", ".join(fields)
        + ")\n#endif\n"
    )
    content += declaration
    destination.write_text(content, encoding="utf-8", newline="\n")


def normalized_xaml_type_info_source_copy(
    source: Path,
    destination: Path,
    projection: str,
    local_headers: tuple[str, ...],
) -> None:
    normalized_generated_copy(source, destination)
    content = destination.read_text(encoding="utf-8")
    marker = '#include "XamlBindingInfo.xaml.g.hpp"\n'
    if content.count(marker) != 1:
        raise RuntimeError(f"expected {projection} XAML binding include")
    includes = f'#include <winrt/{projection}.h>\n' + "".join(
        f'#include "{name}"\n' for name in local_headers
    )
    content = content.replace(marker, marker + includes)
    destination.write_text(content, encoding="utf-8", newline="\n")


def normalized_terminal_app_type_info_source_copy(
    source: Path, destination: Path
) -> None:
    local_types = (
        "AboutDialog",
        "App",
        "ColorPickupFlyout",
        "CommandPalette",
        "FilteredCommand",
        "HighlightedTextControl",
        "MarkdownPaneContent",
        "MinMaxCloseControl",
        "PaletteItemTemplateSelector",
        "SnippetsPaneContent",
        "SuggestionsControl",
        "TabHeaderControl",
        "TabRowControl",
    )
    normalized_xaml_type_info_source_copy(
        source,
        destination,
        "TerminalApp",
        tuple(f"{name}.h" for name in local_types),
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_xaml_symbols(archive: Path) -> None:
    symbols = subprocess.check_output(
        ["x86_64-w64-mingw32-nm", "-C", str(archive)],
        text=True,
    ).splitlines()
    required = (
        "SearchBoxControlT<",
        "TermControlT<",
        "XamlBindings::Loading(",
        "XamlBindingTrackingBase::XamlBindingTrackingBase()",
    )
    for symbol in required:
        if not any(" T " in line and symbol in line for line in symbols):
            raise RuntimeError(f"TerminalControl archive does not define {symbol}")
    print("verified generated page and XAML binding implementations in the archive")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("/tmp/openterminal-mingw"),
        help="completed build_mingw.py root below /tmp",
    )
    parser.add_argument(
        "--wine-prefix",
        type=Path,
        required=True,
        help="Wine prefix below /tmp containing Microsoft .NET Framework 4.8",
    )
    parser.add_argument(
        "--parallel",
        type=int,
        default=2,
        help="parallel MinGW compile jobs (default: 2)",
    )
    parser.add_argument(
        "--skip-tests",
        action="store_true",
        help="compile the archive without rerunning the Wine smoke tests",
    )
    args = parser.parse_args()

    for tool in ("cmake", "ctest", "wine", "x86_64-w64-mingw32-nm"):
        common.require_tool(tool)
    root = require_tmp_path(args.root, "build root")
    wine_prefix = require_tmp_path(args.wine_prefix, "Wine prefix")
    require_file(
        wine_prefix
        / "drive_c"
        / "windows"
        / "Microsoft.NET"
        / "Framework64"
        / "v4.0.30319"
        / "MSBuild.exe",
        "64-bit .NET Framework MSBuild; install dotnet48 in the supplied prefix",
    )
    if args.parallel < 1:
        raise RuntimeError("--parallel must be at least 1")

    pins = json.loads(PINS_FILE.read_text(encoding="utf-8"))
    terminal = root / "windows-terminal"
    if common.git_commit(terminal) != pins["windows_terminal"]["commit"]:
        raise RuntimeError("Windows Terminal checkout does not match phase2/upstreams.json")
    native_build = root / "native-build"
    require_file(native_build / "CMakeCache.txt", "completed MinGW CMake cache")

    sdk_pin = pins["windows_sdk_cpp"]
    sdk_package = require_file(
        root
        / "windows-sdk-cpp"
        / f"{sdk_pin['id'].lower()}.{sdk_pin['version']}.nupkg",
        "pinned Windows SDK package downloaded by build_mingw.py",
    )
    actual_sdk_sha = common.sha256_file(sdk_package)
    if actual_sdk_sha != sdk_pin["sha256"]:
        raise RuntimeError(
            f"Windows SDK package SHA-256 is {actual_sdk_sha}; expected {sdk_pin['sha256']}"
        )
    sdk_base = root / "windows-sdk-cpp" / "extracted" / "c"
    common.extract_windows_sdk(
        sdk_package,
        root / "windows-sdk-cpp" / "extracted",
        sdk_pin["platform_version"],
    )
    sdk_bin = sdk_base / "bin" / sdk_pin["platform_version"] / "x64"
    xaml_tools = sdk_base / "bin" / sdk_pin["platform_version"] / "XamlCompiler"
    mdmerge = require_file(sdk_bin / "mdmerge.exe", "SDK metadata merger")
    require_file(sdk_bin / "midlrtmd.dll", "SDK metadata runtime")
    task_dll = require_file(
        xaml_tools / "Microsoft.Windows.UI.Xaml.Build.Tasks.dll",
        "SDK XAML compiler task",
    )
    require_file(xaml_tools / "x64" / "genxbf.dll", "SDK XBF generator")

    wine_env = os.environ.copy()
    wine_env["WINEPREFIX"] = str(wine_prefix)
    wine_env["WINEDEBUG"] = "-all"
    wine_env["WINEPATH"] = win_path(sdk_bin)

    sdk_references = sdk_base / "References" / sdk_pin["platform_version"]
    winui_metadata = root / "winui-metadata"
    terminalcore_metadata = root / "terminalcore-winmd"
    merged_root = root / "xaml-merged-metadata"
    merged_connection = merge_metadata(
        mdmerge,
        [
            root
            / "terminalconnection-winmd"
            / f"Microsoft.Terminal.TerminalConnection.{name}.winmd"
            for name in TERMINAL_CONNECTION_IDLS
        ],
        [sdk_references],
        merged_root / "TerminalConnection",
        "Microsoft.Terminal.TerminalConnection.winmd",
        wine_env,
    )
    merged_ui = merge_metadata(
        mdmerge,
        [
            root / "uihelpers-winmd" / f"Microsoft.Terminal.UI.{name}.winmd"
            for name in UI_HELPERS_IDLS
        ],
        [sdk_references, winui_metadata],
        merged_root / "UIHelpers",
        "Microsoft.Terminal.UI.winmd",
        wine_env,
    )
    merged_control = merge_metadata(
        mdmerge,
        [
            root / "terminalcontrol-winmd" / f"Microsoft.Terminal.Control.{name}.winmd"
            for name in TERMINAL_CONTROL_IDLS
        ],
        [
            sdk_references,
            winui_metadata,
            terminalcore_metadata,
            merged_connection.parent,
            merged_ui.parent,
        ],
        merged_root / "TerminalControl",
        "Microsoft.Terminal.Control.winmd",
        wine_env,
    )
    merged_markdown = merge_metadata(
        mdmerge,
        [
            root
            / "uimarkdown-winmd"
            / f"Microsoft.Terminal.UI.Markdown.{name}.winmd"
            for name in ("Builder", "CodeBlock")
        ],
        [sdk_references, winui_metadata, merged_ui.parent],
        merged_root / "UIMarkdown",
        "Microsoft.Terminal.UI.Markdown.winmd",
        wine_env,
        namespace_depth=4,
    )
    settings_model_inputs = sorted((root / "settingsmodel-winmd").glob("*.winmd"))
    if len(settings_model_inputs) != 18:
        raise RuntimeError(
            f"expected 18 SettingsModel metadata inputs, found {len(settings_model_inputs)}"
        )
    merged_settings_model = merge_metadata(
        mdmerge,
        settings_model_inputs,
        [
            sdk_references,
            winui_metadata,
            terminalcore_metadata,
            merged_connection.parent,
            merged_ui.parent,
            merged_control.parent,
        ],
        merged_root / "SettingsModel",
        "Microsoft.Terminal.Settings.Model.winmd",
        wine_env,
        namespace_depth=4,
    )
    settings_editor_inputs = sorted((root / "settingseditor-winmd").glob("*.winmd"))
    if len(settings_editor_inputs) != 34:
        raise RuntimeError(
            f"expected 34 SettingsEditor metadata inputs, found {len(settings_editor_inputs)}"
        )
    merged_settings_editor = merge_metadata(
        mdmerge,
        settings_editor_inputs,
        [
            sdk_references,
            winui_metadata,
            terminalcore_metadata,
            merged_connection.parent,
            merged_ui.parent,
            merged_control.parent,
            merged_settings_model.parent,
        ],
        merged_root / "SettingsEditor",
        "Microsoft.Terminal.Settings.Editor.winmd",
        wine_env,
        namespace_depth=4,
    )
    terminal_app_inputs = sorted((root / "terminalapp-winmd").glob("*.winmd"))
    if len(terminal_app_inputs) != 25:
        raise RuntimeError(
            f"expected 25 TerminalApp metadata inputs, found {len(terminal_app_inputs)}"
        )
    merged_terminal_app = merge_metadata(
        mdmerge,
        terminal_app_inputs,
        [
            sdk_references,
            winui_metadata,
            terminalcore_metadata,
            merged_connection.parent,
            merged_ui.parent,
            merged_control.parent,
            merged_settings_model.parent,
            merged_settings_editor.parent,
            merged_markdown.parent,
        ],
        merged_root / "TerminalApp",
        "TerminalApp.winmd",
        wine_env,
        namespace_depth=1,
    )

    control_dir = terminal / "src" / "cascadia" / "TerminalControl"
    pages = [control_dir / "SearchBoxControl.xaml", control_dir / "TermControl.xaml"]
    headers = [control_dir / "SearchBoxControl.h", control_dir / "TermControl.h"]
    references = [
        sdk_base
        / "UnionMetadata"
        / sdk_pin["platform_version"]
        / "Facade"
        / "windows.winmd",
        sdk_references / "Windows.Foundation.FoundationContract.winmd",
        sdk_references / "Windows.Foundation.UniversalApiContract.winmd",
        winui_metadata / "Microsoft.UI.Xaml.winmd",
        terminalcore_metadata / "Microsoft.Terminal.Core.winmd",
        merged_connection,
        merged_ui,
    ]
    for reference in references:
        require_file(reference, "XAML metadata reference")
    include_dirs = [
        control_dir,
        root / "cppwinrt-terminalcontrol",
        root / "cppwinrt-terminalcontrol" / "Microsoft" / "Terminal" / "Control",
        root / "cppwinrt-terminalcore",
        root / "cppwinrt-terminalconnection",
        root / "cppwinrt-uihelpers",
        root / "cppwinrt-winui",
        root / "cppwinrt-sdk",
    ]

    compiler_output = root / "xaml-compiler-output"
    if compiler_output.exists():
        shutil.rmtree(compiler_output)
    compiler_output.mkdir(parents=True)
    project = root / "terminalcontrol-xaml.proj"
    write_msbuild_project(
        project,
        task_dll,
        pages,
        headers,
        references,
        merged_control,
        compiler_output,
        sdk_base,
        sdk_pin["platform_version"],
        include_dirs,
        "Microsoft.Terminal.Control",
        "Microsoft.Terminal.Control",
        "Microsoft.Terminal.Control.Lib",
    )
    msbuild_command = [
        "wine",
        r"C:\windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe",
        win_path(project),
        "/verbosity:normal",
    ]
    run(msbuild_command + ["/target:Pass1"], env=wine_env)
    run(msbuild_command + ["/target:Pass2"], env=wine_env)

    generated_root = root / "xaml-generated" / "Microsoft" / "Terminal" / "Control"
    for name in (
        "SearchBoxControl.xaml.g.h",
        "SearchBoxControl.xaml.g.hpp",
        "TermControl.xaml.g.h",
        "TermControl.xaml.g.hpp",
        "XamlBindingInfo.xaml.g.h",
        "XamlBindingInfo.xaml.g.hpp",
    ):
        normalized_generated_copy(
            require_file(compiler_output / name, "generated XAML header"),
            generated_root / name,
        )
    normalized_metadata_provider_copy(
        require_file(compiler_output / "XamlMetaDataProvider.h", "generated XAML provider"),
        generated_root / "XamlMetaDataProvider.h",
        "Microsoft::Terminal::Control",
    )
    normalized_xaml_type_info_copy(
        require_file(compiler_output / "XamlTypeInfo.xaml.g.h", "generated XAML types"),
        generated_root / "XamlTypeInfo.xaml.g.h",
        "Microsoft::Terminal::Control",
    )
    normalized_generated_copy(
        require_file(compiler_output / "XamlTypeInfo.Impl.g.cpp", "generated XAML provider"),
        generated_root / "XamlTypeInfo.Impl.g.cpp",
    )
    normalized_xaml_type_info_source_copy(
        require_file(compiler_output / "XamlTypeInfo.g.cpp", "generated XAML types"),
        generated_root / "XamlTypeInfo.g.cpp",
        "Microsoft.Terminal.Control",
        ("ScrollBarVisualStateManager.h", "SearchBoxControl.h", "XamlLights.h"),
    )
    for name in ("SearchBoxControl.xbf", "TermControl.xbf"):
        xbf = require_file(compiler_output / name, "generated XBF")
        print(f"{name}: {xbf.stat().st_size} bytes, sha256={sha256(xbf)}")

    markdown_dir = terminal / "src" / "cascadia" / "UIMarkdown"
    markdown_references = [
        sdk_base
        / "UnionMetadata"
        / sdk_pin["platform_version"]
        / "Facade"
        / "windows.winmd",
        sdk_references / "Windows.Foundation.FoundationContract.winmd",
        sdk_references / "Windows.Foundation.UniversalApiContract.winmd",
        winui_metadata / "Microsoft.UI.Xaml.winmd",
        merged_ui,
    ]
    for reference in markdown_references:
        require_file(reference, "UIMarkdown XAML metadata reference")
    markdown_include_dirs = [
        markdown_dir,
        root / "cppwinrt-uimarkdown",
        root
        / "cppwinrt-uimarkdown"
        / "Microsoft"
        / "Terminal"
        / "UI"
        / "Markdown",
        root / "cppwinrt-uihelpers",
        root / "cppwinrt-winui",
        root / "cppwinrt-sdk",
    ]
    markdown_compiler_output = root / "uimarkdown-xaml-compiler-output"
    if markdown_compiler_output.exists():
        shutil.rmtree(markdown_compiler_output)
    markdown_compiler_output.mkdir(parents=True)
    markdown_project = root / "uimarkdown-xaml.proj"
    write_msbuild_project(
        markdown_project,
        task_dll,
        [markdown_dir / "CodeBlock.xaml"],
        [markdown_dir / "CodeBlock.h"],
        markdown_references,
        merged_markdown,
        markdown_compiler_output,
        sdk_base,
        sdk_pin["platform_version"],
        markdown_include_dirs,
        "Microsoft.Terminal.UI.Markdown",
        "Microsoft.Terminal.UI.Markdown",
        "Microsoft.Terminal.UI.Markdown",
    )
    markdown_msbuild_command = [
        "wine",
        r"C:\windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe",
        win_path(markdown_project),
        "/verbosity:normal",
    ]
    run(markdown_msbuild_command + ["/target:Pass1"], env=wine_env)
    run(markdown_msbuild_command + ["/target:Pass2"], env=wine_env)

    markdown_generated_root = (
        root / "xaml-generated" / "Microsoft" / "Terminal" / "UI" / "Markdown"
    )
    for name in (
        "CodeBlock.xaml.g.h",
        "CodeBlock.xaml.g.hpp",
        "XamlBindingInfo.xaml.g.h",
        "XamlBindingInfo.xaml.g.hpp",
    ):
        normalized_generated_copy(
            require_file(markdown_compiler_output / name, "generated UIMarkdown header"),
            markdown_generated_root / name,
        )
    normalized_metadata_provider_copy(
        require_file(
            markdown_compiler_output / "XamlMetaDataProvider.h",
            "generated UIMarkdown provider",
        ),
        markdown_generated_root / "XamlMetaDataProvider.h",
        "Microsoft::Terminal::UI::Markdown",
    )
    normalized_xaml_type_info_copy(
        require_file(
            markdown_compiler_output / "XamlTypeInfo.xaml.g.h",
            "generated UIMarkdown types",
        ),
        markdown_generated_root / "XamlTypeInfo.xaml.g.h",
        "Microsoft::Terminal::UI::Markdown",
    )
    normalized_generated_copy(
        require_file(
            markdown_compiler_output / "XamlTypeInfo.Impl.g.cpp",
            "generated UIMarkdown provider",
        ),
        markdown_generated_root / "XamlTypeInfo.Impl.g.cpp",
    )
    normalized_xaml_type_info_source_copy(
        require_file(
            markdown_compiler_output / "XamlTypeInfo.g.cpp",
            "generated UIMarkdown types",
        ),
        markdown_generated_root / "XamlTypeInfo.g.cpp",
        "Microsoft.Terminal.UI.Markdown",
        (),
    )
    markdown_xbf = require_file(
        markdown_compiler_output / "CodeBlock.xbf", "generated UIMarkdown XBF"
    )
    print(
        f"CodeBlock.xbf: {markdown_xbf.stat().st_size} bytes, "
        f"sha256={sha256(markdown_xbf)}"
    )

    settings_editor_dir = terminal / "src" / "cascadia" / "TerminalSettingsEditor"
    settings_editor_references = [
        sdk_base
        / "UnionMetadata"
        / sdk_pin["platform_version"]
        / "Facade"
        / "windows.winmd",
        sdk_references / "Windows.Foundation.FoundationContract.winmd",
        sdk_references / "Windows.Foundation.UniversalApiContract.winmd",
        winui_metadata / "Microsoft.UI.Xaml.winmd",
        terminalcore_metadata / "Microsoft.Terminal.Core.winmd",
        merged_connection,
        merged_ui,
        merged_control,
        merged_settings_model,
    ]
    for reference in settings_editor_references:
        require_file(reference, "SettingsEditor XAML metadata reference")
    settings_editor_include_dirs = [
        settings_editor_dir,
        terminal / "src" / "cascadia" / "TerminalSettingsAppAdapterLib",
        root / "cppwinrt-settingseditor",
        root
        / "cppwinrt-settingseditor"
        / "Microsoft"
        / "Terminal"
        / "Settings"
        / "Editor",
        root / "cppwinrt-settingsmodel",
        root / "cppwinrt-terminalcontrol",
        root / "cppwinrt-terminalconnection",
        root / "cppwinrt-terminalcore",
        root / "cppwinrt-uihelpers",
        root / "cppwinrt-winui",
        root / "cppwinrt-sdk",
    ]
    settings_editor_compiler_output = root / "settingseditor-xaml-compiler-output"
    if settings_editor_compiler_output.exists():
        shutil.rmtree(settings_editor_compiler_output)
    settings_editor_compiler_output.mkdir(parents=True)
    settings_editor_project = root / "settingseditor-xaml.proj"
    write_msbuild_project(
        settings_editor_project,
        task_dll,
        [
            settings_editor_dir / f"{name}.xaml"
            for name in SETTINGS_EDITOR_PAGES
        ],
        [
            settings_editor_dir / f"{name}.h"
            for name in SETTINGS_EDITOR_CLASS_PAGES
        ],
        settings_editor_references,
        merged_settings_editor,
        settings_editor_compiler_output,
        sdk_base,
        sdk_pin["platform_version"],
        settings_editor_include_dirs,
        "Microsoft.Terminal.Settings.Editor",
        "Microsoft.Terminal.Settings.Editor",
        "Microsoft.Terminal.Settings.Editor",
        use_cppwinrt_local_assembly=False,
    )
    settings_editor_msbuild_command = [
        "wine",
        r"C:\windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe",
        win_path(settings_editor_project),
        "/verbosity:normal",
    ]
    run(settings_editor_msbuild_command + ["/target:Pass1"], env=wine_env)
    run(settings_editor_msbuild_command + ["/target:Pass2"], env=wine_env)

    settings_editor_generated_root = (
        root / "xaml-generated" / "Microsoft" / "Terminal" / "Settings" / "Editor"
    )
    for page in SETTINGS_EDITOR_CLASS_PAGES:
        for suffix in ("xaml.g.h", "xaml.g.hpp"):
            name = f"{page}.{suffix}"
            normalized_generated_copy(
                require_file(
                    settings_editor_compiler_output / name,
                    "generated SettingsEditor header",
                ),
                settings_editor_generated_root / name,
            )
    for name in ("XamlBindingInfo.xaml.g.h", "XamlBindingInfo.xaml.g.hpp"):
        normalized_generated_copy(
            require_file(
                settings_editor_compiler_output / name,
                "generated SettingsEditor binding header",
            ),
            settings_editor_generated_root / name,
        )
    normalized_metadata_provider_copy(
        require_file(
            settings_editor_compiler_output / "XamlMetaDataProvider.h",
            "generated SettingsEditor provider",
        ),
        settings_editor_generated_root / "XamlMetaDataProvider.h",
        "Microsoft::Terminal::Settings::Editor",
    )
    normalized_xaml_type_info_copy(
        require_file(
            settings_editor_compiler_output / "XamlTypeInfo.xaml.g.h",
            "generated SettingsEditor types",
        ),
        settings_editor_generated_root / "XamlTypeInfo.xaml.g.h",
        "Microsoft::Terminal::Settings::Editor",
    )
    normalized_generated_copy(
        require_file(
            settings_editor_compiler_output / "XamlTypeInfo.Impl.g.cpp",
            "generated SettingsEditor provider",
        ),
        settings_editor_generated_root / "XamlTypeInfo.Impl.g.cpp",
    )
    normalized_xaml_type_info_source_copy(
        require_file(
            settings_editor_compiler_output / "XamlTypeInfo.g.cpp",
            "generated SettingsEditor types",
        ),
        settings_editor_generated_root / "XamlTypeInfo.g.cpp",
        "Microsoft.Terminal.Settings.Editor",
        (
            "Actions.h",
            "AddProfile.h",
            "Appearances.h",
            "ArgsTemplateSelectors.h",
            "ColorSchemes.h",
            "TerminalColorConverters.h",
            "Compatibility.h",
            "EditAction.h",
            "EditColorScheme.h",
            "Extensions.h",
            "GlobalAppearance.h",
            "IconPicker.h",
            "Interaction.h",
            "KeyChordListener.h",
            "Launch.h",
            "NewTabMenu.h",
            "NullableColorPicker.h",
            "Profiles_Advanced.h",
            "Profiles_Appearance.h",
            "Profiles_Base.h",
            "Profiles_Base_Orphaned.h",
            "Profiles_Terminal.h",
            "Rendering.h",
            "SettingContainer.h",
        ),
    )
    for page in SETTINGS_EDITOR_PAGES:
        output = settings_editor_compiler_output
        xbf = require_file(output / f"{page}.xbf", "generated SettingsEditor XBF")
        print(f"{page}.xbf: {xbf.stat().st_size} bytes, sha256={sha256(xbf)}")

    terminal_app_dir = terminal / "src" / "cascadia" / "TerminalApp"
    terminal_app_references = [
        sdk_base
        / "UnionMetadata"
        / sdk_pin["platform_version"]
        / "Facade"
        / "windows.winmd",
        sdk_references / "Windows.Foundation.FoundationContract.winmd",
        sdk_references / "Windows.Foundation.UniversalApiContract.winmd",
        winui_metadata / "Microsoft.UI.Xaml.winmd",
        terminalcore_metadata / "Microsoft.Terminal.Core.winmd",
        merged_connection,
        merged_ui,
        merged_control,
        merged_settings_model,
        merged_settings_editor,
        merged_markdown,
    ]
    for reference in terminal_app_references:
        require_file(reference, "TerminalApp XAML metadata reference")
    terminal_app_include_dirs = [
        terminal_app_dir,
        terminal / "src" / "cascadia" / "TerminalSettingsAppAdapterLib",
        root / "cppwinrt-terminalapp",
        root / "cppwinrt-terminalapp" / "TerminalApp",
        root / "cppwinrt-settingseditor",
        root / "cppwinrt-settingsmodel",
        root / "cppwinrt-uimarkdown",
        root / "cppwinrt-terminalcontrol",
        root / "cppwinrt-terminalconnection",
        root / "cppwinrt-terminalcore",
        root / "cppwinrt-uihelpers",
        root / "cppwinrt-winui",
        root / "cppwinrt-sdk",
    ]
    terminal_app_compiler_output = root / "terminalapp-xaml-compiler-output"
    if terminal_app_compiler_output.exists():
        shutil.rmtree(terminal_app_compiler_output)
    terminal_app_compiler_output.mkdir(parents=True)
    terminal_app_project = root / "terminalapp-xaml.proj"
    write_msbuild_project(
        terminal_app_project,
        task_dll,
        [terminal_app_dir / f"{name}.xaml" for name in TERMINAL_APP_PAGES],
        [terminal_app_dir / f"{name}.h" for name in TERMINAL_APP_CLASS_PAGES],
        terminal_app_references,
        merged_terminal_app,
        terminal_app_compiler_output,
        sdk_base,
        sdk_pin["platform_version"],
        terminal_app_include_dirs,
        "TerminalApp",
        "TerminalApp",
        "TerminalAppLib",
        use_cppwinrt_local_assembly=False,
        code_generation_control_flags="DoNotGenerateOtherProviders",
    )
    terminal_app_msbuild_command = [
        "wine",
        r"C:\windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe",
        win_path(terminal_app_project),
        "/verbosity:normal",
    ]
    run(terminal_app_msbuild_command + ["/target:Pass1"], env=wine_env)
    run(terminal_app_msbuild_command + ["/target:Pass2"], env=wine_env)

    terminal_app_generated_root = root / "xaml-generated" / "TerminalApp"
    for page in TERMINAL_APP_CLASS_PAGES:
        for suffix in ("xaml.g.h", "xaml.g.hpp"):
            name = f"{page}.{suffix}"
            source = require_file(
                terminal_app_compiler_output / name,
                "generated TerminalApp header",
            )
            destination = terminal_app_generated_root / name
            normalized_generated_copy(source, destination)
            if name == "App.xaml.g.hpp":
                content = destination.read_text(encoding="utf-8")
                old = "template struct AppT<struct App>;"
                new = (
                    "template struct AppT<struct App, "
                    "::winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider>;"
                )
                if content.count(old) != 1:
                    raise RuntimeError("expected generated TerminalApp AppT instantiation")
                destination.write_text(
                    content.replace(old, new), encoding="utf-8", newline="\n"
                )
    for name in ("XamlBindingInfo.xaml.g.h", "XamlBindingInfo.xaml.g.hpp"):
        normalized_generated_copy(
            require_file(
                terminal_app_compiler_output / name,
                "generated TerminalApp binding header",
            ),
            terminal_app_generated_root / name,
        )
    normalized_metadata_provider_copy(
        require_file(
            terminal_app_compiler_output / "XamlMetaDataProvider.h",
            "generated TerminalApp metadata provider",
        ),
        terminal_app_generated_root / "XamlMetaDataProvider.h",
        "TerminalApp",
    )
    normalized_xaml_type_info_copy(
        require_file(
            terminal_app_compiler_output / "XamlTypeInfo.xaml.g.h",
            "generated TerminalApp type metadata",
        ),
        terminal_app_generated_root / "XamlTypeInfo.xaml.g.h",
        "TerminalApp",
    )
    normalized_generated_copy(
        require_file(
            terminal_app_compiler_output / "XamlTypeInfo.Impl.g.cpp",
            "generated TerminalApp metadata provider",
        ),
        terminal_app_generated_root / "XamlTypeInfo.Impl.g.cpp",
    )
    normalized_terminal_app_type_info_source_copy(
        require_file(
            terminal_app_compiler_output / "XamlTypeInfo.g.cpp",
            "generated TerminalApp metadata provider",
        ),
        terminal_app_generated_root / "XamlTypeInfo.g.cpp",
    )
    for page in TERMINAL_APP_PAGES:
        xbf = require_file(
            terminal_app_compiler_output / f"{page}.xbf",
            "generated TerminalApp XBF",
        )
        print(f"{page}.xbf: {xbf.stat().st_size} bytes, sha256={sha256(xbf)}")

    xamlcore_root = root / "xamlcore-runtime"
    run(
        [
            sys.executable,
            str(PHASE2_DIR.parent / "phase3" / "scripts" / "build_xamlcore.py"),
            "--root",
            str(xamlcore_root),
            "--dll-only",
            "--no-xvfb",
        ]
    )
    xamlcore_dll = require_file(
        xamlcore_root / "Microsoft.UI.Xaml.dll", "OpenXaml runtime DLL"
    )
    xamlcore_import_library = require_file(
        xamlcore_root / "libopenxaml.dll.a", "OpenXaml import library"
    )

    run(
        [
            "cmake",
            "-S",
            str(PHASE2_DIR),
            "-B",
            str(native_build),
            f"-DOPENTERMINAL_XAML_GENERATED_DIR={root / 'xaml-generated'}",
            f"-DOPENTERMINAL_XAMLCORE_IMPORT_LIBRARY={xamlcore_import_library}",
        ]
    )
    run(
        [
            "cmake",
            "--build",
            str(native_build),
            "--target",
            "openterminal_terminal_control_model",
            "openterminal_ui_markdown",
            "openterminal_settings_editor",
            "openterminal_terminal_app",
            "openterminal_windows_terminal",
            "--parallel",
            str(args.parallel),
        ]
    )
    # Deploy compiled markup in the same ms-appx-relative shape used by the
    # generated InitializeComponent methods. These remain temporary build
    # artifacts beside WindowsTerminal.exe; no XBF enters the repository.
    xbf_deployments = (
        (compiler_output, "Microsoft.Terminal.Control"),
        (markdown_compiler_output, "Microsoft.Terminal.UI.Markdown"),
        (settings_editor_compiler_output, "Microsoft.Terminal.Settings.Editor"),
        (terminal_app_compiler_output, "TerminalApp"),
    )
    deployed_xbf = 0
    for source_dir, namespace in xbf_deployments:
        destination_dir = native_build / namespace
        destination_dir.mkdir(parents=True, exist_ok=True)
        for source in sorted(source_dir.glob("*.xbf")):
            shutil.copy2(source, destination_dir / source.name)
            deployed_xbf += 1
    print(f"deployed {deployed_xbf} XBF files below {native_build}")

    # PRI generation is tied to MSBuild's packaging pipeline, which this
    # source-only MinGW build intentionally does not invoke. Preserve the same
    # application resource-map boundaries and localized values in a
    # deterministic textual catalog consumed by openxaml.dll. The catalog is a
    # generated build artifact beside the XBF tree, never repository content.
    resource_catalog = native_build / "OpenXaml" / "resources.json"
    resource_catalog.parent.mkdir(parents=True, exist_ok=True)
    run([
        sys.executable,
        str(PHASE2_DIR.parent / "phase3" / "scripts" / "distil_resw_strings.py"),
        str(terminal),
        "--locale", "en-US",
        "--out", str(resource_catalog),
    ])
    print(f"deployed OpenXaml resource catalog: {resource_catalog}")
    shutil.copy2(xamlcore_dll, native_build / "Microsoft.UI.Xaml.dll")
    print(f"deployed OpenXaml runtime: {native_build / 'Microsoft.UI.Xaml.dll'}")
    verify_xaml_symbols(native_build / "libMicrosoft.Terminal.Control.Model.a")
    terminal_exe = require_file(
        native_build / "WindowsTerminal.exe",
        "linked WindowsTerminal executable",
    )
    print(
        f"WindowsTerminal.exe: {terminal_exe.stat().st_size} bytes, "
        f"sha256={sha256(terminal_exe)}"
    )
    if not args.skip_tests:
        run(["ctest", "--test-dir", str(native_build), "--output-on-failure"])
    print(
        "WinUI XAML, TerminalControl, UIMarkdown, SettingsEditor, TerminalApp, "
        "and WindowsTerminal completed "
        f"under {root}"
    )


if __name__ == "__main__":
    main()
