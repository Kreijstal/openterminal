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

## WinUI 2 / XAML

The `Microsoft.UI.Xaml` 2.8.4 harvester records the UI framework surface that
Windows Terminal consumes:

- every WinRT type, GUID, method-signature blob, property, event, enum field,
  and implemented interface in `Microsoft.UI.Xaml.winmd`;
- documented public member identifiers from the XML contract;
- `Generic.xaml` namespaces, resource keys, named elements, styles, control
  templates, and element counts;
- framework-package identity, minimum OS version, native activation classes,
  DLL exports, and DLL imports for x86, x64, ARM, and ARM64;
- the NuGet and nested AppX file manifests with sizes and SHA-256 hashes; and
- MSBuild imports, targets, properties, package registrations, references, and
  the conditional VCLibs dependency.

The PRI files are currently hashed and sized. A semantic PRI dump remains a
separate task because it requires `MakePri` or an open PRI parser.
