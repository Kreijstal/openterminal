# Microsoft.UI.Xaml 2.8.4 ABI and resource surface

This snapshot describes the WinUI 2 dependency consumed by Microsoft Terminal
commit `fbda436dc654cf551dd196b2667ef95d3e0a7262`.

- Package: `Microsoft.UI.Xaml`
- Version: `2.8.4`
- Framework package version: `8.2305.5001.0`
- Package SHA-256:
  `486dbe1f271b31057ec01ecb7ce4df3eedad11efdc26569170fb128ee4a4e964`
- Source: NuGet.org V3 package feed
- Harvest: `abi.json`

No NuGet, AppX, DLL, WinMD, PRI, XBF, image, or signature payload is committed.

## Contract scale

The WinMD contains 623 type definitions: 399 interfaces, 182 classes, 41
enums, and one struct. Those types expose 3,382 methods, 1,918 properties, 118
events, 195 fields, and 230 interface implementations. Each method retains its
exact ECMA-335 signature blob, parameter sequence, flags, and owning type. Each
WinRT interface with a `GuidAttribute` retains its decoded IID.

The public XML documentation adds 224 documented types, 266 methods, 962
properties, 56 events, and 164 fields. The WinMD references the Windows
Foundation and Universal API contracts plus `Microsoft.Web.WebView2.Core`.

## Native activation surface

The x86, x64, ARM, and ARM64 framework packages each register 136 identical
activatable classes. Their DLLs export the same four entry points:

| Ordinal | Export |
| ---: | --- |
| 1 | `DllGetActivationFactory` |
| 2 | `DllCanUnloadNow` |
| 3 | `DllMain` |
| 4 | `SendTelemetryOnSuspend` |

The x64 DLL has 32 static import-DLL groups. Apart from API-set, Universal CRT,
Kernel32, and OleAut32 imports, its only direct graphics DLL import is
`d2d1.dll`. Broader XAML and Composition behavior is reached through WinRT
activation rather than a conventional import library.

The AppX WinMD files are not byte-identical across architectures, but the
harvested metadata contracts are identical. The package-level WinMD is
byte-identical to the x86 AppX copy and semantically identical to every copy.

## XAML and packaging surface

`Generic.xaml` contains 3,246 elements, 227 keyed resources, 819 named
elements, 73 styles, and 47 control templates. The framework manifest requires
Windows 10 build 17763 or newer and registers `Microsoft.UI.Xaml.dll` as an
in-process WinRT server.

The MSBuild integration:

- adds the WinMD reference used by C++/WinRT projection;
- selects architecture-specific framework AppX packages;
- conditionally adds `Microsoft.VCLibs, Version=14.0` for app-container
  executables;
- enforces target platform build 18362 and minimum platform build 17763; and
- moves WinMD and PRI resources into packaging outputs.

The PRI payloads are currently recorded by path, size, and SHA-256. Their
semantic resource maps require a later `MakePri` oracle harvest or an open PRI
parser.

## Reproduce

```bash
python3 -m pip install -r phase1/requirements-winui.txt
python3 phase1/scripts/download_nuget_package.py \
  --source https://api.nuget.org/v3/index.json \
  --package Microsoft.UI.Xaml \
  --version 2.8.4 \
  --expected-sha256 486dbe1f271b31057ec01ecb7ce4df3eedad11efdc26569170fb128ee4a4e964 \
  --output /tmp/microsoft.ui.xaml.2.8.4.nupkg

python3 phase1/scripts/harvest_winui_xaml.py \
  /tmp/microsoft.ui.xaml.2.8.4.nupkg \
  --source https://api.nuget.org/v3/index.json \
  --output /tmp/winui-xaml-abi.json
```
