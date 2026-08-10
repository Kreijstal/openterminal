# Windows Terminal MinGW snapshot

- Upstream repository: <https://github.com/microsoft/terminal>
- Upstream commit: `e74649d5f18b1c123556461b30f763407aea558f`
- Commit date: `2026-08-03T17:14:39+02:00`
- Generated inventories: `phase0/scripts/harvest_dependencies.py` and
  `phase0/scripts/harvest_build_surface.py`

The deterministic inventories contain 117 parsed MSBuild roots, 414 imports,
324 references, 111 IDL inputs, 48 XAML inputs, and 307 RESW inputs, with no
XML parse failures.

## Open build result

Using GCC 16.1.0 targeting `x86_64-w64-mingw32`:

- fmt 12.1.0, GSL 4.2.2, CLI11 2.6.1, cmark 0.31.1, jsoncpp 1.9.6, and ICU
  78.3 built from source with Terminal's exact vcpkg baseline
  `927f62e4b8838bd7e441e9c45103a16ffd75007e`;
- all six parser translation units compiled into `libConTermParser.a`;
- all four TerminalInput, fifteen ConTypes, fifteen ConBufferOut, and nine
  ConTermAdapt upstream translation units compiled into their respective
  static libraries;
- all nine ConRenderBase, five ConRenderGdi, two ConRenderUia, twelve
  ConRenderAtlas, and five TerminalCore translation units compiled into their
  respective static libraries;
- all four WinRTUtils, six UIHelpers, three TSF, two MidiAudio, four WinConPty,
  and seven TerminalConnection upstream translation units compiled into six
  additional static libraries;
- all fifteen TerminalControl IDLs produced WinMD and an optimized C++/WinRT
  component projection;
- the base build compiled thirteen TerminalControl C++ units, then the pinned
  SDK XAML compiler emitted genuine page headers, page implementations, shared
  binding code, and both XBF files;
- with that output, all fifteen upstream TerminalControl C++ units compiled
  into `libMicrosoft.Terminal.Control.Model.a`, including
  `SearchBoxControl.cpp` and `TermControl.cpp`;
- all five TerminalConnection and all five UIHelpers IDLs produced temporary
  WinMD and optimized component projections;
- Wine `widl` generated the classic Terminal handoff COM header, while MIDLRT
  was isolated to the MIDL3-to-WinMD steps that `widl` cannot yet parse;
- checksum-pinned `Microsoft.UI.Xaml` 2.8.4 and WebView2 1.0.1661.34 WinMD
  inputs produced a working MinGW C++/WinRT compile-time projection;
- all four Atlas HLSL programs compiled into temporary DXBC headers using the
  checksum-pinned SDK shader tool under Wine;
- `wt.exe` and `elevate-shim.exe`, including their resource scripts and icon,
  linked with MinGW under `/tmp`;
- GCC compiled every unit except `adaptDispatchGraphics.cpp`, which Clang 22
  compiled for the same x86-64 mingw-w64 GNU/COFF ABI; and
- a clean build completed 191 native steps, after which all nineteen x64 PE
  behavior and ABI tests passed under Wine 11.13.

MIDL3 inputs were converted to temporary WinMD with MIDLRT from the
checksum-pinned `Microsoft.Windows.SDK.CPP` 10.0.26100.1 package, then projected
by the MinGW-built open-source C++/WinRT 2.0.250303.1 generator. The package
payload, MIDLRT, `fxc`, generated WinMD, projections, DXBC, objects, archives,
and PE files all remained below `/tmp`.

The remaining application boundary is the aggregate XAML metadata provider,
PRI packaging, WinUI runtime activation, and the settings/application projects,
not MFC. This Terminal revision contains no MFC headers or MFC project
declarations, and WPF's managed `System.Windows.*` ABI cannot satisfy native
WinUI 2. Reproduce the base with `phase2/scripts/build_mingw.py`, then run
`phase2/scripts/build_winui_xaml.py` for both XAML passes and the complete
TerminalControl archive.
