# Phase 1: metadata and behavior harvesting

Harvest the minimum metadata needed to understand WinMD, XAML/XBF, PRI, imports,
activation, and baseline startup behavior. Proprietary inputs may be inspected
by CI runners, but only reviewable textual descriptions and hashes belong in
the repository.

## TerminalThemeHelpers

The first Phase 1 target is the native
`Microsoft.Internal.Windows.Terminal.ThemeHelpers` package. Its ABI harvester
downloads the exact hash-pinned package and records:

- the three C header declarations;
- x86, x64, and ARM64 PE machine and address-size metadata;
- export names and ordinals;
- imported DLLs and symbols;
- import-library symbol decoration;
- MSBuild include, link, architecture-selection, and copy-local behavior; and
- SHA-256 hashes for package contents.

The NuGet archive and its DLL/import-library payloads remain temporary. CI
publishes only the normalized JSON harvest.
