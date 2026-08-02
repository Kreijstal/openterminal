# TerminalThemeHelpers 0.8.250811004 ABI

This snapshot describes the native ABI consumed by Microsoft Terminal commit
`fbda436dc654cf551dd196b2667ef95d3e0a7262`.

- Package: `Microsoft.Internal.Windows.Terminal.ThemeHelpers`
- Version: `0.8.250811004`
- Package SHA-256:
  `c70c34c5ed0b937a23285402396c3f23523496dca59ede2aaddc2914f6ea339d`
- Source: TerminalDependencies NuGet V3 feed declared by Microsoft Terminal
- Harvest: `abi.json`

No package binary or NuGet archive is committed.

## Exported ABI

| Ordinal | C export | Signature |
| ---: | --- | --- |
| 1 | `TerminalTrySetAutoCompleteAnimationsWhenOccluded` | `HRESULT(IUnknown*, bool)` |
| 2 | `TerminalTrySetTransparentBackground` | `HRESULT(const bool)` |
| 3 | `TerminalTrySetWindowAssociatedProcesses` | `HRESULT(HWND, DWORD, PHANDLE)` |

The export names and ordinals are identical for x86, x64, and ARM64. The header
uses `extern "C"` when compiled as C++. It declares no explicit calling
convention. The x86 import library exposes `_Function` and `__imp__Function`
symbols without an `@N` suffix, which is consistent with MSVC `__cdecl`. x64
and ARM64 expose the ordinary undecorated C names required by those ABIs.

The DLLs import only Windows API-set and Universal CRT entry points directly.
They import `LoadLibraryExW`, `GetProcAddress`, and `RoGetActivationFactory`, so
some feature-specific behavior is resolved dynamically and cannot be identified
from the static import table alone.

The package metadata contains copyright text but no NuGet `license` or
`licenseUrl` declaration.

## Reproduce

```bash
python3 phase1/scripts/download_nuget_package.py \
  --source 'https://pkgs.dev.azure.com/shine-oss/terminal/_packaging/TerminalDependencies%40Local/nuget/v3/index.json' \
  --package Microsoft.Internal.Windows.Terminal.ThemeHelpers \
  --version 0.8.250811004 \
  --expected-sha256 c70c34c5ed0b937a23285402396c3f23523496dca59ede2aaddc2914f6ea339d \
  --output /tmp/themehelpers.nupkg

python3 phase1/scripts/harvest_themehelpers_abi.py \
  /tmp/themehelpers.nupkg \
  --source 'https://pkgs.dev.azure.com/shine-oss/terminal/_packaging/TerminalDependencies%40Local/nuget/v3/index.json' \
  --output /tmp/themehelpers-abi.json
```
