# OpenTerminal research

OpenTerminal is a harvest-first research project for building Microsoft
Windows Terminal with an open-source toolchain and running it on Wine.

The project records reproducible facts before attempting broad compatibility
work. CI probes may use a Windows installation as an oracle, but committed
research data must remain reviewable text or JSON. Microsoft binaries, SDK
payloads, NuGet archives, generated applications, and CI binaries are never
committed.

## Project phases

- **Phase 0:** inventory dependencies, tools, packages, and build capabilities.
- **Phase 1:** harvest WinMD, XAML/XBF, PRI, imports, and baseline runtime data.
- **Phase 2:** generate and validate an open-source build path.
- **Phase 3:** launch progressively larger Terminal components under Wine.
- **Phase 4:** validate the complete UI and ConPTY behavior, then split reusable
  fixes into upstreamable Wine patches.

The implementation targets are intentionally kept separate:

- Terminal build/source changes belong in a Terminal fork when they become
  necessary.
- Wine compatibility changes belong in
  [wine-kreijstal](https://github.com/Kreijstal/wine-kreijstal).
- Harvesters, research snapshots, and cross-project orchestration live here.

See [wine-kreijstal issue #5](https://github.com/Kreijstal/wine-kreijstal/issues/5)
for the umbrella compatibility tracker.

## Running Phase 0 locally

```bash
git clone https://github.com/microsoft/terminal /tmp/windows-terminal
python3 phase0/scripts/harvest_dependencies.py \
  /tmp/windows-terminal \
  --output /tmp/dependency-inventory.json
python3 phase0/scripts/harvest_build_surface.py \
  /tmp/windows-terminal \
  --output /tmp/build-surface.json
python3 -m json.tool /tmp/dependency-inventory.json >/dev/null
python3 -m json.tool /tmp/build-surface.json >/dev/null
```

The Phase 0 GitHub Actions workflow performs the same harvest and publishes the
results as a CI artifact.

## Running Phase 2 locally

The base open build compiles Terminal's native libraries with x86-64
mingw-w64. A second, isolated SDK harvest runs both genuine WinUI XAML passes,
emits the TerminalControl, SettingsEditor, and TerminalApp XBF files, and links
the real `WindowsTerminal.exe` GUI host with generated metadata providers. The
build also produces the `wt` and elevation launcher PEs, generates the Terminal
WinRT projections, and executes nineteen focused PE tests with Wine. Open
manifest dependencies and C++/WinRT build from source. All downloaded and
generated artifacts remain below `/tmp`:

With Phase 3's activation DLL registered, that executable now creates its
desktop HWND/XAML island and remains alive for the complete 30-second Phase 4
boot probe with no missing import, runtime class, or exception. Visible XBF UI,
rendering, input, accessibility, and ConPTY integration remain Phase 4 work.

```bash
python3 -B phase2/scripts/build_mingw.py --root /tmp/openterminal-mingw

WINEPREFIX=/tmp/openterminal-dotnet48 \
  WINEARCH=win64 \
  XDG_CACHE_HOME=/tmp/openterminal-winetricks-cache \
  winetricks -q dotnet48
python3 -B phase2/scripts/build_winui_xaml.py \
  --root /tmp/openterminal-mingw \
  --wine-prefix /tmp/openterminal-dotnet48
```

See [`phase2/README.md`](phase2/README.md) for the exact revisions, manual CMake
commands, compatibility scope, and current WinUI/XAML boundary.
