# Phase 2: open build generation

Phase 2 now has a reproducible mingw-w64 build through the real
`WindowsTerminal.exe` host. The base build covers Terminal's parser, input,
buffer, adapter, renderer, core, connection, UI-helper, TSF, audio, ConPTY, and
TerminalControl layers. The XAML follow-up additionally builds SettingsModel,
the settings adapter, UIMarkdown, SettingsEditor, TerminalApp, their generated
metadata providers and XBF-backed page implementations, and the eight-source
WindowsTerminal GUI host.

The build consumes exact upstream revisions without modifying their checkouts,
builds the `wt.exe` and `elevate-shim.exe` launchers, links nineteen focused
x64 PE tests, and runs them under Wine. A clean run completes 191 native build
steps and all nineteen tests.

## Reproduce the build

The one-command driver clones every source at the commits in
[`upstreams.json`](upstreams.json), builds Terminal's vcpkg dependencies for
`x64-mingw-static`, builds the C++/WinRT compiler with MinGW, builds the native
targets above, and runs their tests under Wine. It uses pinned temporary
harvests for MIDL3-to-WinMD, Atlas HLSL-to-DXBC, and the WinUI 2/WebView2 WinMD
inputs. Projection generation returns to the MinGW-built open C++/WinRT tool:

```bash
python3 -B phase2/scripts/build_mingw.py --root /tmp/openterminal-mingw
```

The script refuses build roots outside `/tmp` and refuses to reuse a checkout
at an unexpected commit. It requires CMake, Ninja, Git, Python, Wine, an x86-64
mingw-w64 GCC toolchain, and Clang with a mingw-w64 target. vcpkg itself must
have full Git history because manifest versioning checks out historical port
trees. The driver downloads the exact `Microsoft.Windows.SDK.CPP` package
listed in `upstreams.json`, verifies its SHA-256, and selectively extracts only
MIDLRT, `fxc`, their required DLLs, XAML tools and platform descriptors, two IDL
support files, and WinMD references under `/tmp`. It never copies that SDK
payload into the repository.

### Finish the XAML-backed TerminalControl units

The follow-up driver needs a temporary Wine prefix with Microsoft .NET
Framework 4.8 because the harvested SDK XAML task targets .NET Framework. This
keeps the framework installer, cache, prefix, SDK tools, generated WinMD, XAML
headers, and XBF outside the repository:

```bash
WINEPREFIX=/tmp/openterminal-dotnet48 \
  WINEARCH=win64 \
  XDG_CACHE_HOME=/tmp/openterminal-winetricks-cache \
  winetricks -q dotnet48

python3 -B phase2/scripts/build_winui_xaml.py \
  --root /tmp/openterminal-mingw \
  --wine-prefix /tmp/openterminal-dotnet48
```

The driver verifies the pinned Windows SDK package hash, selectively extracts
`mdmerge`, the XAML task, `genxbf`, and their data, merges the temporary WinMD,
runs XAML passes 1 and 2 under Wine, normalizes generated include casing, and
reconfigures the existing MinGW build. It then verifies that the archive
defines the generated page and binding symbols, links `WindowsTerminal.exe`,
and reruns all nineteen tests. Both compiler passes complete with zero warnings
and zero errors. The resulting archives contain all fifteen upstream
TerminalControl C++ units plus the SettingsEditor and TerminalApp XAML sources;
the binary XBF and PE files remain under the selected `/tmp` root. Mono `xbuild` can load the
task but currently stops inside its Windows `CreateFile` call, so the .NET
Framework Wine prefix is an explicit harvested-tool prerequisite for this
stage. The smoke suite validates the archive and its dependency surface but
does not activate a WinUI control; that still requires a usable WinUI runtime.

For an already prepared source/dependency tree, configure the native target
set directly:

```bash
cmake -S phase2 -B /tmp/openterminal-native -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/phase2/toolchains/x64-mingw-static.cmake" \
  -DCMAKE_PREFIX_PATH=/tmp/openterminal-mingw/installed/x64-mingw-static \
  -DTERMINAL_SOURCE_DIR=/tmp/openterminal-mingw/windows-terminal \
  -DWIL_SOURCE_DIR=/tmp/openterminal-mingw/wil \
  -DCPPWINRT_SDK_INCLUDE_DIR=/tmp/openterminal-mingw/cppwinrt-sdk \
  -DCPPWINRT_TERMINALCORE_INCLUDE_DIR=/tmp/openterminal-mingw/cppwinrt-terminalcore \
  -DCPPWINRT_TERMINALCONNECTION_INCLUDE_DIR=/tmp/openterminal-mingw/cppwinrt-terminalconnection \
  -DCPPWINRT_WINUI_INCLUDE_DIR=/tmp/openterminal-mingw/cppwinrt-winui \
  -DCPPWINRT_UIHELPERS_INCLUDE_DIR=/tmp/openterminal-mingw/cppwinrt-uihelpers \
  -DCPPWINRT_TERMINALCONTROL_INCLUDE_DIR=/tmp/openterminal-mingw/cppwinrt-terminalcontrol \
  -DWINDOWS_SDK_BIN_DIR=/tmp/openterminal-mingw/windows-sdk-cpp/extracted/c/bin/10.0.26100.0/x64 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/openterminal-native --parallel
ctest --test-dir /tmp/openterminal-native --output-on-failure
```

After the XAML follow-up has generated its temporary output, add
`-DOPENTERMINAL_XAML_GENERATED_DIR=/tmp/openterminal-mingw/xaml-generated` to
include the two XAML-backed units in a manual configuration.

## Current open-toolchain surface

The following exact dependencies build from source for `x64-mingw-static`:
fmt 12.1.0, Microsoft GSL 4.2.2, CLI11 2.6.1, cmark 0.31.1, jsoncpp 1.9.6,
and ICU 78.3. The vcpkg baseline, version overrides, ICU source archive hash,
and ICU upstream release commit are pinned. C++/WinRT 2.0.250303.1 also has an
upstream CMake path intended for mingw-w64 and produces a working
`cppwinrt.exe`.

The parser target contains all six translation units from Terminal's parser
project: the input and output engines, state machine, Base64 helper, tracing,
and precompiled-header unit. TerminalInput adds all four translation units from
its upstream project. ConTypes contains all fifteen upstream translation units,
including its five UI Automation provider units. A focused classic-COM
implementation of WRL's missing `wrl/implements.h` supplies the reference
counting, `QueryInterface`, `RuntimeClass`, and `MakeAndInitialize` surface they
use.

ConBufferOut contains all fifteen upstream translation units plus one small
compatibility unit that selects the generic instruction-set path. ConTermAdapt
contains all nine translation units in Terminal's adapter project. GCC compiles
eight adapter units; Clang compiles `adaptDispatchGraphics.cpp` for the same
mingw-w64 GNU/COFF ABI because GCC cannot resolve two `switch` expressions that
combine Terminal's templated enum conversion and integer conversion. This is a
compiler-language edge case, not a proprietary dependency, and the upstream
source remains untouched.

ConRenderBase contains all nine renderer-base units, ConRenderGdi all five GDI
units, and ConRenderUia both UI Automation units. ConRenderAtlas contains all
twelve upstream C++ units. Its four HLSL programs are compiled into temporary
DXBC headers with the checksum-pinned SDK `fxc` under Wine; the headers and
shader tool never enter the repository. Renderer Wine tests cover CSS length
resolution, construction of the GDI engine against MinGW's GDI and Uniscribe
imports, delivery of queued UIA text events, and Atlas glyph/gamma helpers.
TerminalCore adds all five upstream units and a Wine-tested x64 PE. Static link
groups close the intentional renderer/buffer cycles, and MinGW's `mincore`,
`ntdll`, and `bcrypt` imports replace MSVC's implicit SDK link behavior.

The later native layer is no longer just an inventory. WinRTUtils contains all
four upstream units. Microsoft.Terminal.UI contains all six UIHelpers units
and generated component glue. ConTSF contains all three TSF units, with public
TSF declarations and `GUID_PROP_COMPOSING` supplied where mingw-w64's headers
or `libuuid.a` omit them. MidiAudio contains both upstream units. WinConPty
contains its two project units plus the two server units it owns, and
TerminalConnection contains all seven upstream units plus generated component
glue. Wine tests exercise TerminalConnection activation, callbacks, and the
Echo connection; the launcher test verifies `wt.exe`'s expected missing-target
path.

The driver compiles all five TerminalConnection and five UIHelpers IDLs to
temporary WinMD, then produces optimized component projections. Wine `widl`
generates the classic `ITerminalHandoff` COM header, so that path is fully
open. MIDL3 is a separate boundary: TerminalCore, TerminalConnection,
UIHelpers, and TerminalControl use syntax that current Wine `widl` does not
parse. The reproducible fallback harvests MIDLRT from the SHA-256-pinned
`Microsoft.Windows.SDK.CPP` 10.0.26100.1 package, creates only temporary WinMD,
then hands projection back to the open-source C++/WinRT tool.

For the UI ABI, the driver verifies and extracts only
`Microsoft.UI.Xaml.winmd` 2.8.4 and `Microsoft.Web.WebView2.Core.winmd`
1.0.1661.34 from checksum-pinned packages. The MinGW-built C++/WinRT generator
successfully projects both metadata sets, and a Wine test validates selected
interface GUIDs. This supplies compile-time ABI metadata, not the WinUI runtime
or its activation factories.

All fifteen TerminalControl IDLs compile to temporary WinMD and a component
projection. The base archive contains thirteen C++ units; the XAML follow-up
uses the real generated contracts and implementations to compile all fifteen.
The archive defines `InitializeComponent`, connector, and binding support rather
than relying on declaration-only stubs. Localized MinGW preparation removes a
duplicate SDK enum-operator set, makes four HRESULT switch labels explicitly
unsigned, and preserves the upstream checkout. The integration test pulls in
ControlCore, ControlInteractivity, Atlas, TSF, UIA, TerminalConnection, and
UIHelpers rather than testing only a header-only model.

A deterministic Python generator translates the pinned `src/features.xml`
into the forced `TilFeatureStaging.h` header, replacing that project's
PowerShell/MSBuild generation step. Compatibility headers provide:

- case-sensitive aliases for Windows SDK header spellings;
- public WinRT weak-reference declarations from mingw-w64;
- GCC equivalents for the small set of MSVC intrinsics used by WIL;
- the classic-COM subset of WRL used by Terminal's accessibility providers;
- UI Automation, TSF, swap-chain-panel, and shader-reflection declarations or
  UUIDs missing from mingw-w64; and
- a no-op TraceLogging provider, because ETW tracing is diagnostic rather than
  parser behavior.

The build maps Terminal's Windows-style flattened ICU declarations to the
official open ICU headers and normalizes one pinned Terminal header's
MSVC-only include separator. Those generated text files and every object,
archive, and PE remain outside the repository.

No Microsoft binaries, NuGet packages, SDK payloads, or generated PE files are
stored in this repository.

## XAML: WinUI is the relevant implementation

Windows Terminal uses native C++/WinRT and **WinUI 2**
(`Microsoft.UI.Xaml` 2.8.4), not WPF. WPF's XAML object model is managed
`System.Windows.*`; it cannot satisfy WinUI's WinRT classes, activation
factories, WinMD contracts, or XBF/PRI resource format.

WPF was still probed from source at its pinned commit. Its open
`PresentationBuildTasks` and architecture-neutral WindowsDesktop SDK compile
on Linux for `net11.0` and `net472` with the SDK pinned by WPF's `global.json`.
The full `PresentationFramework` graph reaches the native
`DirectWriteForwarder.vcxproj` and requires Visual C++ targets. The directly
relevant WinUI 2.8.4 source is pinned and harvested under
`research/microsoft-ui-xaml`. The temporary SDK harvest now proves Terminal's
XAML code and XBF stages; fully open replacements for that compiler plus the
remaining MSBuild/C++/CX, MIDL, PRI, and runtime stages are still future work.

Reproduce the focused open WPF build independently; its SDK, packages, source,
and outputs also remain below `/tmp`:

```bash
python3 -B phase2/scripts/build_wpf_xaml.py --root /tmp/openterminal-wpf
```

The real WindowsTerminal host and aggregate application metadata provider now
compile and link. Together with Phase 3's open activation DLL, the host also
boots under Wine, creates its desktop HWND/XAML island, and remains alive for
the 30-second Phase 4 probe without missing imports, runtime classes, or an
exception. This is not yet a deployable Windows Terminal package: PRI-backed
resource fidelity, XBF materialization, visible controls, rendering, and input
remain integration work. The current MIDLRT, `fxc`, and XAML compiler
steps are deliberately isolated checksum-pinned SDK harvests under `/tmp`;
replacing them with open MIDL3, HLSL-to-DXBC, and WinUI XAML implementations
remains separate toolchain work.

OpenMFC is not on the dependency path: the pinned Terminal source has no MFC
headers or MFC project declarations. Adding an MFC compatibility library would
therefore not advance this build.
