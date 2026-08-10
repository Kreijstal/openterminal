# WPF open-build observation

- Upstream repository: <https://github.com/dotnet/wpf>
- Upstream commit: `2ca037562c207924e53cfcc99286e523d3694de3`
- Commit date: `2026-08-03T09:52:15+03:00`

The pinned open-source .NET SDK compiled
`Microsoft.NET.Sdk.WindowsDesktop.ArchNeutral.csproj` successfully on Linux.
That build compiled WPF's `PresentationBuildTasks` for both `net11.0` and
`net472` with zero warnings and zero errors.

The deterministic reproducer checks out this exact commit and keeps the pinned
.NET SDK, NuGet packages, source tree, and managed outputs under `/tmp`:

```bash
python3 phase2/scripts/build_wpf_xaml.py --root /tmp/openterminal-wpf
```

A focused build of `PresentationFramework.csproj` progressed through numerous
managed reference and cycle-breaker assemblies, then failed when
`DirectWriteForwarder.vcxproj` imported the unavailable
`Microsoft.Cpp.Default.props`. This is the first native Visual C++ build-system
barrier; other reported failures were API-compat tooling, a missing `net6.0`
restore target, and preview-API diagnostics.

WPF is not a substitute for Terminal's `Microsoft.UI.Xaml` dependency. WPF
exposes managed `System.Windows.*` types, while Terminal consumes native WinUI
2 WinRT metadata, activation classes, and XBF/PRI resources. This observation
only establishes which WPF XAML build tools can already be compiled with the
open .NET toolchain.
